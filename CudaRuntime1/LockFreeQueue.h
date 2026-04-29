#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H

#include <atomic>
#include <memory>

template<typename T>
class LockFreeQueue {
public:
    LockFreeQueue();
    ~LockFreeQueue();

    void enqueue(T value);
    bool dequeue(T& result);
    int size() const;

private:
    struct Node {
        T data;
        std::atomic<Node*> next;

        Node(T value) : data(value), next(nullptr) {}
    };

    std::atomic<Node*> head;
    std::atomic<Node*> tail;
    std::atomic<int> queueSize;
};

template<typename T>
LockFreeQueue<T>::LockFreeQueue() : queueSize(0) {
    Node* dummy = new Node(T());  // 哑节点
    head.store(dummy);
    tail.store(dummy);
}

template<typename T>
LockFreeQueue<T>::~LockFreeQueue() {
    while (Node* old = head.load()) {
        head.store(old->next);
        delete old;
    }
}

template<typename T>
void LockFreeQueue<T>::enqueue(T value) {
    Node* newNode = new Node(value);
    Node* oldTail = tail.load();
    while (!tail.compare_exchange_weak(oldTail, newNode)) {
        // 保证尾指针的更新是线程安全的
    }
    oldTail->next.store(newNode);

    queueSize.fetch_add(1, std::memory_order_relaxed);  // 入队时，增加队列大小
}

template<typename T>
bool LockFreeQueue<T>::dequeue(T& result) {
    Node* oldHead = head.load();
    Node* newHead = oldHead->next.load();
    if (newHead == nullptr) {
        return false;  // 队列为空
    }
    result = newHead->data;
    head.store(newHead);  // 更新头指针

    queueSize.fetch_sub(1, std::memory_order_relaxed);  // 出队时，减少队列大小

    delete oldHead;
    return true;
}

template<typename T>
int LockFreeQueue<T>::size() const {
    return queueSize.load(std::memory_order_relaxed);  // 返回队列大小
}

#endif // LOCK_FREE_QUEUE_H
