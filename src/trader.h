#pragma once

#include <atomic>

struct Trader {
  Trader(unsigned id) : _id(id) {
    _balance = 0;
  }

  bool increaseBalance(unsigned value) {
    _balance += value;
    return true;
  }

  bool decreaseBalance(unsigned value) {
    if (value == 0)
      return true;

    unsigned oldBalance, newBalance;
    do {
      oldBalance = _balance;
      newBalance = oldBalance < value ? oldBalance : oldBalance - value;
    }
    while (!_balance.compare_exchange_strong(oldBalance, newBalance));

    return oldBalance != newBalance;
  }

  unsigned getBalance() const {
    return _balance;
  }

  unsigned getId() const {
    return _id;
  }

private:
  std::atomic<unsigned> _balance;

  const unsigned _id;
};
