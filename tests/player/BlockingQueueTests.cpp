#include <QtTest/QtTest>

#include "player/BlockingQueue.h"

#include <atomic>
#include <chrono>
#include <thread>

class BlockingQueueTests : public QObject {
  Q_OBJECT

private slots:
  void pushesAndPopsInOrder();
  void waitsUntilItemIsPushed();
  void closeWakesWaitingConsumers();
  void clearRemovesPendingItems();
  void rejectsPushAfterClose();
  void pushWaitsWhenQueueIsFull();
  void closeWakesWaitingProducers();
};

void BlockingQueueTests::pushesAndPopsInOrder() {
  BlockingQueue<int> queue;

  QVERIFY(queue.push(1));
  QVERIFY(queue.push(2));
  QCOMPARE(queue.size(), static_cast<std::size_t>(2));

  int first = 0;
  int second = 0;
  QVERIFY(queue.waitPop(first));
  QVERIFY(queue.waitPop(second));

  QCOMPARE(first, 1);
  QCOMPARE(second, 2);
  QCOMPARE(queue.size(), static_cast<std::size_t>(0));
}

void BlockingQueueTests::waitsUntilItemIsPushed() {
  BlockingQueue<int> queue;
  std::atomic_bool consumerStarted(false);
  int value = 0;

  std::thread consumer([&]() {
    consumerStarted.store(true);
    QVERIFY(queue.waitPop(value));
  });

  while (!consumerStarted.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  QVERIFY(queue.push(42));
  consumer.join();

  QCOMPARE(value, 42);
}

void BlockingQueueTests::closeWakesWaitingConsumers() {
  BlockingQueue<int> queue;
  std::atomic_bool popReturned(false);
  bool popSucceeded = true;
  int value = 0;

  std::thread consumer([&]() {
    popSucceeded = queue.waitPop(value);
    popReturned.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  queue.close();
  consumer.join();

  QVERIFY(popReturned.load());
  QVERIFY(!popSucceeded);
}

void BlockingQueueTests::clearRemovesPendingItems() {
  BlockingQueue<int> queue;

  QVERIFY(queue.push(7));
  QVERIFY(queue.push(8));
  queue.clear();

  QCOMPARE(queue.size(), static_cast<std::size_t>(0));

  int value = 0;
  QVERIFY(!queue.tryPop(value));
}

void BlockingQueueTests::rejectsPushAfterClose() {
  BlockingQueue<int> queue;

  queue.close();

  QVERIFY(queue.isClosed());
  QVERIFY(!queue.push(1));
  QCOMPARE(queue.size(), static_cast<std::size_t>(0));
}

void BlockingQueueTests::pushWaitsWhenQueueIsFull() {
  BlockingQueue<int> queue(1);

  QVERIFY(queue.push(1));
  std::atomic_bool secondPushReturned(false);

  std::thread producer([&]() {
    queue.push(2);
    secondPushReturned.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  QVERIFY(!secondPushReturned.load());

  int value = 0;
  QVERIFY(queue.waitPop(value));
  QCOMPARE(value, 1);

  producer.join();
  QVERIFY(secondPushReturned.load());
  QCOMPARE(queue.size(), static_cast<std::size_t>(1));

  QVERIFY(queue.waitPop(value));
  QCOMPARE(value, 2);
}

void BlockingQueueTests::closeWakesWaitingProducers() {
  BlockingQueue<int> queue(1);

  QVERIFY(queue.push(1));
  std::atomic_bool pushReturned(false);
  bool pushSucceeded = true;

  std::thread producer([&]() {
    pushSucceeded = queue.push(2);
    pushReturned.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  queue.close();
  producer.join();

  QVERIFY(pushReturned.load());
  QVERIFY(!pushSucceeded);
  QCOMPARE(queue.size(), static_cast<std::size_t>(1));
}

QTEST_MAIN(BlockingQueueTests)
#include "BlockingQueueTests.moc"
