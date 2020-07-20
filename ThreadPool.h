class ThreadPool {
  public:
    explicit ThreadPool( size_t );

    void enqueue( std::function<void()>&& f );

    ~ThreadPool() = default;

    void stop();

  private:
    std::vector<std::thread> workers;

    std::function<void()> task;
    int                   worker_num_;          // 只会被初始化一次
    int                   free_worker_num_ = 0; // queue_mutex

    bool have_task = false;
    bool is_stop   = false;

    std::mutex              queue_mutex;
    std::condition_variable condition_new_task;
    std::condition_variable condition_finish_task;
};

inline ThreadPool::ThreadPool( size_t thread_num )
    : worker_num_( thread_num ) {

    for ( size_t i = 0; i < thread_num; ++i ) {

        workers.emplace_back( [this] {
            for ( ;; ) {
                std::function<void()> task_;
                {
                    std::unique_lock<std::mutex> lock( this->queue_mutex );
                    free_worker_num_++;
                    this->condition_new_task.wait( lock, [this] { return this->is_stop || this->have_task; } );

                    if ( !this->have_task and this->is_stop ) break;
                    this->have_task = false;
                    free_worker_num_--;
                    task_ = std::move( this->task );
                    this->condition_finish_task.notify_one(); // 避免 this->have_task = true 的时候 async_enqueue 卡死
                }

                task_();

                std::unique_lock<std::mutex> lock( this->queue_mutex );
                if ( this->is_stop ) break;
                this->condition_finish_task.notify_one();
            }
        } );
    }
}

inline void ThreadPool::enqueue( std::function<void()>&& f ) {
    std::unique_lock<std::mutex> lock( queue_mutex );

    this->condition_finish_task.wait( lock, [this] { return this->free_worker_num_ != 0 and !this->have_task; } );

    this->task      = std::move( f );
    this->have_task = true;
    this->condition_new_task.notify_one();
}

inline void ThreadPool::stop() {
    {
        std::unique_lock<std::mutex> lock( queue_mutex );
        is_stop = true;
        condition_new_task.notify_all();
    }
    for ( std::thread& worker : workers )
        worker.join();
}