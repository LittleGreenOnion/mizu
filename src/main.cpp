#include "exchange_engine.h"
#include <iostream>

using namespace std;

void threadTest() {
  ExchangeEngine engine;

  shared_ptr<Trader> tr0 = make_shared<Trader>(0);
  shared_ptr<Trader> tr1 = make_shared<Trader>(1);

  atomic<unsigned> exchangeId;
  exchangeId = 0;

  tr0->increaseBalance(10000);
  tr1->increaseBalance(10000);

  auto foo = [&](shared_ptr<Trader> client) {
    for (int i = 0; i != 1000; ++i) {
      unsigned price = rand() % 200;
      unsigned quantity = rand() % 10;
      bool side = rand() % 2;
      engine.placeOrder({client, exchangeId++, price, quantity, side, false});
    }
  };

  thread th0(foo, tr0);
  thread th1(foo, tr1);
  th0.join();
  th1.join();

  std::this_thread::sleep_for(std::chrono::seconds(1));
  cout << "tr0 " << tr0->getBalance() << endl;
  cout << "tr1 " << tr1->getBalance() << endl;
  engine.print();

  cout << "Transaction History" << endl;
  VariadicTable<unsigned, unsigned, unsigned, unsigned> vt({"exchange id sell order", "exchange id buy order", "sold", "price"});
  for (const auto& item : engine.getLastTransactions(999))
    vt.addRow(item._exchangeIdSeller, item._exchangeIdBuyer, item._sold, item._price);
  vt.print(std::cout);
}

int main() {
  // Playground
  ExchangeEngine engine;

  shared_ptr<Trader> tr0 = make_shared<Trader>(0);
  shared_ptr<Trader> tr1 = make_shared<Trader>(1);

  atomic<unsigned> exchangeId;
  exchangeId = 0;

  engine.placeOrder({tr0, exchangeId++, 100, 1, true, false}); // sell
  engine.print();
  engine.placeOrder({tr1, exchangeId++, 100, 1, false, false}); // buy
  engine.print();

  tr1->increaseBalance(100);
  engine.print();
  std::this_thread::sleep_for(std::chrono::seconds(6));
  engine.print();


  engine.placeOrder({tr0, exchangeId++, 100, 1, true, false});
  engine.placeOrder({tr0, exchangeId++, 110, 2, true, false});
  engine.placeOrder({tr0, exchangeId++, 120, 3, true, false});
  engine.placeOrder({tr0, exchangeId++, 140, 5, true, false});
  engine.placeOrder({tr0, exchangeId++, 150, 6, true, false});
  engine.print();

  engine.placeOrder({tr1, exchangeId++, 90, 1, false, false});
  engine.placeOrder({tr1, exchangeId++, 100, 2, false, false});
  engine.placeOrder({tr1, exchangeId++, 110, 3, false, false});
  engine.placeOrder({tr1, exchangeId++, 120, 4, false, false});
  engine.placeOrder({tr1, exchangeId++, 130, 5, false, false});
  engine.print();

  tr0->increaseBalance(1000);
  tr1->increaseBalance(1000);
  engine.placeOrder({tr1, exchangeId++, 140, 6, false, false});
  engine.print();

  cout << "tr0 " << tr0->getBalance() << endl;
  cout << "tr1 " << tr1->getBalance() << endl;

  engine.cancelOrder(4, true);
  engine.placeOrder({tr1, exchangeId++, 0, 50, false, true});
  engine.print();
  cout << "tr0 " << tr0->getBalance() << endl;
  cout << "tr1 " << tr1->getBalance() << endl;

  tr1->increaseBalance(10000);
  engine.placeOrder({tr0, exchangeId++, 0, 25, true, true});
  engine.print();
  cout << "tr0 " << tr0->getBalance() << endl;
  cout << "tr1 " << tr1->getBalance() << endl;
  std::this_thread::sleep_for(std::chrono::seconds(6));
  engine.print();


  cout << "Transaction History" << endl;
  VariadicTable<unsigned, unsigned, unsigned, unsigned> vt({"exchange id sell order", "exchange id buy order", "sold", "price"});
  for (const auto& item : engine.getLastTransactions(999))
    vt.addRow(item._exchangeIdSeller, item._exchangeIdBuyer, item._sold, item._price);
  vt.print(std::cout);


  return 0;
}
