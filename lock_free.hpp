//
// Created by dm on 5/24/21.
//

#ifndef PARPROG1__LOCK_FREE_HPP_
#define PARPROG1__LOCK_FREE_HPP_
#include <optional>
#include "atomic_sp.hpp"

template<typename T>
struct Node {
  SharedPtr<Node> next;
  T data;
};

template<typename T>
class LockFreeStack {
 public:
  LockFreeStack() {}

  void push(const T &data);
  std::optional<T> pop();

 private:
  AtomicSharedPtr<Node<T> > top_;
};

template<typename T>
void LockFreeStack<T>::push(const T &data) {
  auto newTop = SharedPtr<Node<T>>(new Node<T>);
  newTop->next = top_.get();
  newTop->data = data;
  while (!top_.compareExchange(newTop->next.get(), std::move(newTop))) {
    newTop->next = top_.get();
  }
}

template<typename T>
std::optional<T> LockFreeStack<T>::pop() {
  auto res = top_.getFast();
  if (res.get() == nullptr)
    return {};

  while (!top_.compareExchange(res.get(), res.get()->next.copy())) {
    res = top_.getFast();
    if (res.get() == nullptr)
      return std::nullopt;
  }

  return res.get()->data;
}

#endif //PARPROG1__LOCK_FREE_HPP_
