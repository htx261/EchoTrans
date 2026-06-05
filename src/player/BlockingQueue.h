#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>

template <typename T>
class BlockingQueue {
public:
  bool push(T item) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return false;
      }

      queue_.push(std::move(item));
    }

    condition_.notify_one();
    return true;
  }

  bool waitPop(T& item) {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this]() {
      return closed_ || !queue_.empty();
    });

    if (queue_.empty()) {
      return false;
    }

    item = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  bool tryPop(T& item) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return false;
    }

    item = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::queue<T> empty;
    queue_.swap(empty);
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }

    condition_.notify_all();
  }

  bool isClosed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::queue<T> queue_;
  bool closed_ = false;
};
