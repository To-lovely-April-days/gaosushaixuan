// ============================================================================
// 文件名：LockFreeQueue.h
// 作用  ：无锁队列模板类，用于跨线程的生产者-消费者通信
//         本项目中三处使用：
//           1) 相机线程入队帧 → GPU 线程出队
//           2) GPU 线程入队结果 → 分发线程出队
//           3) 心跳字节队列
//
// 实现思路：
//   - 单链表 + atomic 原子指针
//   - tail 指针用 CAS（compare_exchange_weak）保证多生产者安全
//   - 当前 dequeue 实现假设单消费者（多消费者会有内存释放竞争问题）
// ============================================================================
#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H

#include <atomic>
#include <memory>

template<typename T>
class LockFreeQueue {
public:
    LockFreeQueue();
    ~LockFreeQueue();

    void enqueue(T value);             // 入队（生产者调用）
    bool dequeue(T& result);            // 出队（消费者调用）；返回 false 表示队列空
    int  size() const;                  // 当前队列长度（近似值，仅供监控参考）

private:
    // 链表节点
    struct Node {
        T data;                         // 元素值
        std::atomic<Node*> next;        // 指向下一节点的原子指针

        Node(T value) : data(value), next(nullptr) {}
    };

    std::atomic<Node*> head;           // 队头（消费者操作端）
    std::atomic<Node*> tail;           // 队尾（生产者操作端）
    std::atomic<int>   queueSize;      // 当前长度
};

// ============================================================================
// 构造：建一个哑节点（dummy node），头尾都指向它
// 这样可以避免空队列的特殊处理
// ============================================================================
template<typename T>
LockFreeQueue<T>::LockFreeQueue() : queueSize(0) {
    Node* dummy = new Node(T());
    head.store(dummy);
    tail.store(dummy);
}

// ============================================================================
// 析构：依次释放所有节点
// ============================================================================
template<typename T>
LockFreeQueue<T>::~LockFreeQueue() {
    while (Node* old = head.load()) {
        head.store(old->next);
        delete old;
    }
}

// ============================================================================
// enqueue: 入队
//   1) 创建新节点
//   2) 用 CAS 原子地把 tail 指向新节点
//   3) 把旧 tail 的 next 指向新节点（让链表连起来）
//
// 注意：步骤 2 和步骤 3 之间不是原子的，理论上可能短暂破坏链表完整性
//       但由于本项目使用模式简单（单生产者）所以可以接受
// ============================================================================
template<typename T>
void LockFreeQueue<T>::enqueue(T value) {
    Node* newNode = new Node(value);
    Node* oldTail = tail.load();
    while (!tail.compare_exchange_weak(oldTail, newNode)) {
        // CAS 失败时 oldTail 会被自动更新为最新值，重试
    }
    oldTail->next.store(newNode);
    queueSize.fetch_add(1, std::memory_order_relaxed);
}

// ============================================================================
// dequeue: 出队
//   返回 false：队列为空
//   返回 true ：result 写入第一个有效元素值
// ============================================================================
template<typename T>
bool LockFreeQueue<T>::dequeue(T& result) {
    Node* oldHead = head.load();
    Node* newHead = oldHead->next.load();
    if (newHead == nullptr) {
        return false;   // 没有数据
    }
    result = newHead->data;     // 取走数据
    head.store(newHead);         // 头指针前移
    queueSize.fetch_sub(1, std::memory_order_relaxed);
    delete oldHead;              // 释放旧的哑节点
    return true;
}

// ============================================================================
// size: 返回近似长度
// 注意：这是计数器读值，不保证与实际链表长度严格一致（多线程下有微小延迟）
// ============================================================================
template<typename T>
int LockFreeQueue<T>::size() const {
    return queueSize.load(std::memory_order_relaxed);
}

#endif // LOCK_FREE_QUEUE_H
