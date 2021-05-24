//
// Created by dm on 5/24/21.
//

#ifndef PARPROG1__ATOMIC_SP_HPP_
#define PARPROG1__ATOMIC_SP_HPP_
#include <atomic>
#include <vector>

#define get_value(ptr) (ptr & 0x0000'0000'0000'FFFF)

static const int MAX_BATCH = (1 << 14);
template<typename T>
struct ControlBlock {
  explicit ControlBlock(T *data)
      : data(data), refCount(1) {}

  T *data;
  std::atomic<std::size_t> refCount; // inner counter (negative)
};

//Implementation of atomic shared pointer with packed pointers (like in c++20)
//Ref: https://www.1024cores.net/home/lock-free-algorithms/object-life-time-management/differential-reference-counting/
//Ref: https://github.com/facebook/folly/blob/master/folly/concurrency/AtomicSharedPtr.h
// Inner counter in Control block, outer - in Packed pointer.


template<typename T>
class SharedPtr {
 public:
  SharedPtr(): controlBlock(nullptr) {}
  explicit SharedPtr(T *data)
      : controlBlock(new ControlBlock<T>(data))
  {}
  explicit SharedPtr(ControlBlock<T> *controlBlock): controlBlock(controlBlock) {}
  SharedPtr(const SharedPtr &other) {
    controlBlock = other.controlBlock;
    if (controlBlock != nullptr) {
      controlBlock->refCount.fetch_add(1);
    }
  };
  SharedPtr(SharedPtr &&other) noexcept {
    controlBlock = other.controlBlock;
    other.controlBlock = nullptr;
  };
  SharedPtr& operator=(const SharedPtr &other) {
    auto old = controlBlock;
    controlBlock = other.controlBlock;
    if (controlBlock != nullptr) {
      controlBlock->refCount.fetch_add(1);
    }
    unref(old);
    return *this;
  };
  SharedPtr& operator=(SharedPtr &&other) {
    if (controlBlock != other.controlBlock) {
      auto old = controlBlock;
      controlBlock = other.controlBlock;
      other.controlBlock = nullptr;
      unref(old);
    }
    return *this;
  }
  ~SharedPtr() {
    thread_local std::vector<ControlBlock<T>*> destructionQueue;
    thread_local bool destructionInProgress = false;

    destructionQueue.push_back(controlBlock);
    if (!destructionInProgress) {
      destructionInProgress = true;
      while (destructionQueue.size()) {
        ControlBlock<T> *blockToUnref = destructionQueue.back();
        destructionQueue.pop_back();
        unref(blockToUnref);
      }
      destructionInProgress = false;
    }
  }

  SharedPtr copy() { return SharedPtr(*this); }
  T* get() const { return controlBlock ? controlBlock->data : nullptr; }
  T* operator->() const { return controlBlock->data; }

 private:
  void unref(ControlBlock<T> *blockToUnref) {
    if (blockToUnref) {
      int before = blockToUnref->refCount.fetch_sub(1);
      if (before == 1) {
        delete blockToUnref->data;
        delete blockToUnref;
      }
    }
  }

  template<typename A> friend class AtomicSharedPtr;
  ControlBlock<T>* controlBlock;
};


template<typename T>
class FastSharedPtr {
 public:
  FastSharedPtr(const FastSharedPtr<T> &other) = delete;
  FastSharedPtr(FastSharedPtr<T> &&other)
      : knownValue(other.knownValue)
      , foreignPackedPtr(other.foreignPackedPtr)
      , data(other.data)
  {
    other.foreignPackedPtr = nullptr;
  };
  FastSharedPtr& operator=(FastSharedPtr<T> &&other) {
    destroy();
    knownValue = other.knownValue;
    foreignPackedPtr = other.foreignPackedPtr;
    data = other.data;
    other.foreignPackedPtr = nullptr;
    return *this;
  }
  ~FastSharedPtr() {
    destroy();
  };

  ControlBlock<T>* getControlBlock() { return reinterpret_cast<ControlBlock<T>*>(knownValue >> 16); }
  T* get() { return data; }
  T* operator->(){ return data; }
 private:
  void destroy() {
    if (foreignPackedPtr != nullptr) {
      size_t expected = knownValue;
      while (!foreignPackedPtr->compare_exchange_weak(expected, expected - 1)) {
        if (((expected >> 16) != (knownValue >> 16)) || !(get_value(expected))) {
          ControlBlock<T> *block = reinterpret_cast<ControlBlock<T>*>(knownValue >> 16);
          size_t before = block->refCount.fetch_sub(1);
          if (before == 1) {
            delete data;
            delete block;
          }
          break;
        }
      }
    }
  }
  FastSharedPtr(std::atomic<size_t> *packedPtr)
      : knownValue(packedPtr->fetch_add(1) + 1)
      , foreignPackedPtr(packedPtr)
      , data(getControlBlock()->data)
  {
    auto block = getControlBlock();
    int diff = get_value(knownValue);
    while (diff > 1000 && block == getControlBlock()) {
      block->refCount.fetch_add(diff);
      if (packedPtr->compare_exchange_strong(knownValue, knownValue - diff)) {
        foreignPackedPtr = nullptr;
        break;
      }
      block->refCount.fetch_sub(diff);
      diff = get_value(knownValue);
    }
  };

  size_t knownValue;
  std::atomic<size_t> *foreignPackedPtr;
  T *data;

  template<typename A> friend class AtomicSharedPtr;
};


template<typename T>
class AtomicSharedPtr {
 public:
  AtomicSharedPtr(T *data = nullptr);
  ~AtomicSharedPtr();

  AtomicSharedPtr(const AtomicSharedPtr &other) = delete;
  AtomicSharedPtr(AtomicSharedPtr &&other) = delete;
  AtomicSharedPtr& operator=(const AtomicSharedPtr &other) = delete;
  AtomicSharedPtr& operator=(AtomicSharedPtr &&other) = delete;

  SharedPtr<T> get();
  FastSharedPtr<T> getFast();

  bool compareExchange(T *expected, SharedPtr<T> &&newOne); // this actually is strong version

  void store(T *data);
  void store(SharedPtr<T>&& data);

 private:
  void destroyOldControlBlock(size_t oldPackedPtr);

  std::atomic<size_t> packedPtr;
  static_assert(sizeof(T*) == sizeof(size_t));
};

template<typename T>
AtomicSharedPtr<T>::AtomicSharedPtr(T *data) {
  auto block = new ControlBlock(data);
  packedPtr.store(reinterpret_cast<size_t>(block) << 16);
}

template<typename T>
SharedPtr<T> AtomicSharedPtr<T>::get() {
  size_t packedPtrCopy = packedPtr.fetch_add(1);
  auto block = reinterpret_cast<ControlBlock<T>*>(packedPtrCopy >> 16);
  block->refCount.fetch_add(1);

  size_t expected = packedPtrCopy + 1;
  while (true) {
    if (packedPtr.compare_exchange_strong(expected, expected - 1)) {
      break;
    }

    if (((expected >> 16) != (packedPtrCopy >> 16)) ||
        (get_value(expected) == 0)) {
      block->refCount.fetch_sub(1);
      break;
    }
  }
  return SharedPtr<T>(block);
}

template<typename T>
FastSharedPtr<T> AtomicSharedPtr<T>::getFast() {
  return FastSharedPtr<T>(&packedPtr);
}

template<typename T>
AtomicSharedPtr<T>::~AtomicSharedPtr() {
  thread_local std::vector<size_t> destructionQueue;
  thread_local bool destructionInProgress = false;

  size_t packedPtrCopy = packedPtr.load();
  auto block = reinterpret_cast<ControlBlock<T>*>(packedPtrCopy >> 16);
  size_t diff = get_value(packedPtrCopy);
  if (diff != 0) {
    block->refCount.fetch_add(diff);
  }

  destructionQueue.push_back(packedPtrCopy);
  if (!destructionInProgress) {
    destructionInProgress = true;
    while (destructionQueue.size()) {
      size_t controlBlockToDestroy = destructionQueue.back();
      destructionQueue.pop_back();
      destroyOldControlBlock(controlBlockToDestroy);
    }
    destructionInProgress = false;
  }
}

template<typename T>
void AtomicSharedPtr<T>::store(T *data) {
  store(SharedPtr<T>(data));
}

template<typename T>
void AtomicSharedPtr<T>::store(SharedPtr<T> &&data) {
  while (true) {
    auto holder = this->getFast();
    if (compareExchange(holder.get(), std::move(data))) {
      break;
    }
  }
}

template<typename T>
bool AtomicSharedPtr<T>::compareExchange(T *expected, SharedPtr<T> &&newOne) {
  if (expected == newOne.get()) {
    return true;
  }
  auto holder = this->getFast();
  if (holder.get() == expected) {
    size_t holdedPtr = reinterpret_cast<size_t>(holder.getControlBlock());
    size_t desiredPackedPtr = reinterpret_cast<size_t>(newOne.controlBlock) << 16;
    size_t expectedPackedPtr = holdedPtr << 16;
    while (holdedPtr == (expectedPackedPtr >> 16)) {
      if (get_value(expectedPackedPtr)) {
        int diff = get_value(expectedPackedPtr);
        holder.getControlBlock()->refCount.fetch_add(diff);
        if (!packedPtr.compare_exchange_weak(expectedPackedPtr, (expectedPackedPtr >> 16) << 16)) {
          holder.getControlBlock()->refCount.fetch_sub(diff);
        }
        continue;
      }
      if (packedPtr.compare_exchange_weak(expectedPackedPtr, desiredPackedPtr)) {
        newOne.controlBlock = nullptr;
        destroyOldControlBlock(expectedPackedPtr);
        return true;
      }
    }
  }

  return false;
}

template<typename T>
void AtomicSharedPtr<T>::destroyOldControlBlock(size_t oldPackedPtr) {

  auto block = reinterpret_cast<ControlBlock<T>*>(oldPackedPtr >> 16);
  auto refCountBefore = block->refCount.fetch_sub(1);
  if (refCountBefore == 1) {
    delete block->data;
    delete block;
  }
}

#endif //PARPROG1__ATOMIC_SP_HPP_
