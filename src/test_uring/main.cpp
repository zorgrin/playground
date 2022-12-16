#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <cassert>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <liburing.h>

class Logger final
{
public:
    Logger(const char *function, int line)
    {
        m_stream << function << "@" << line;
    }

    Logger() = default;

    ~Logger()
    {
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);

        std::cerr << m_stream.str() << std::endl;
    }


    template <typename T>
    Logger& operator<<(const T &value)
    {
        if (m_stream.tellp() != 0)
        {
            m_stream << " ";
        }
        m_stream << value;
        return *this;
    }


private:
    std::stringstream m_stream;
};

#define Log() Logger(__PRETTY_FUNCTION__, __LINE__)

class FileDescriptor final
{
public:
    enum class Mode : int
    {
        Read = O_RDONLY,
        Write = O_WRONLY,
    };

    FileDescriptor(const FileDescriptor &) = delete;

    FileDescriptor(const std::string &name, Mode mode)
        : m_fd(::open(name.c_str(), static_cast<int>(mode), 0))
    {
        if (m_fd < 0)
        {
            throw std::system_error(errno, std::generic_category(), std::string(std::strerror(errno)));
        }
    }


    FileDescriptor(int fd)
        : m_fd(fd)
    {
    }

    ~FileDescriptor()
    {
        cleanup();
    }

    int handle() const
    {
        return m_fd;
    }

    void cleanup()
    {
        if (m_fd >= 0)
        {
            ::close(m_fd);
        }
    }

private:
    const int m_fd;
};

namespace uring
{

class Context final
{
public:

    class Executor;

    class Entry final
    {
    public:
        Entry(const Entry &) = delete;

        Entry(Entry &&other)
            : m_d(std::move(other.m_d))

        {
        }

        Entry()
            : m_d(new Private())

        {
        }

        ~Entry()
        {
            assert(m_d->complete);
        }

        void finalize(::io_uring_sqe *handle)
        {

            if (!handle)
            {
                throw std::invalid_argument("invalid entry handle");
            }
            static_assert(sizeof(this) == sizeof(decltype(::io_uring_sqe::user_data)), "only 64-bit system is supported");
            handle->user_data = reinterpret_cast<decltype(::io_uring_sqe::user_data)>(this);
            // Log() << "entry" << this << "handle" << handle;
        }

        void wait()
        {
            std::unique_lock<std::mutex> lock(m_d->mutex);
            m_d->condition.wait(lock, [this]() -> bool
            {
                return m_d->complete;
            });
        }

        void lock()
        {
            m_d->mutex.lock();
        }

        void unlock()
        {
            m_d->mutex.unlock();
        }

    private:
        friend class Executor;

        void markComplete()
        {
            m_d->complete = true;
            m_d->condition.notify_one();
        }

        struct Private
        {
            std::mutex mutex;
            std::atomic<bool> complete;
            std::condition_variable condition;
        };
        std::unique_ptr<Private> m_d;
    };

    class Executor final
    {
    public:
        Executor(const std::vector<Entry *> &entries)
            : m_entries(entries)
        {
            // TODO nullptr sanity check 
        }

        ~Executor()
        {
            // TODO complete all entries anyway
        }

        bool exec(Context &context);
    private:
        Executor(const Executor &) = delete;

        void wake(Entry &entry)
        {
            std::unique_lock<Entry> lock(entry);
            entry.markComplete();
        }

        std::vector<Entry *> m_entries;
    };


    class Parameters
    {
    public:
        Parameters() = default;

        Parameters(std::uint32_t inputCapacity, std::uint32_t outputCapacity)
        {
            m_data.sq_entries = inputCapacity;
            m_data.cq_entries = outputCapacity;
            m_data.flags = IORING_SETUP_IOPOLL;
        }
    
    
        ::io_uring_params &data()
        {
            return m_data;
        }

    private:
        ::io_uring_params m_data = {};
    };

    Context(Parameters &parameters)
    {
        constexpr std::size_t entries = 1024;
        if (int result = ::io_uring_queue_init_params(entries, &m_data, &parameters.data()); result != 0)
        {
            throw std::system_error(-result, std::generic_category(), std::string(std::strerror(-result)));
        }
    }

    ~Context()
    {
        ::io_uring_queue_exit(&m_data);
    }

    [[nodiscard]] Entry prepareEntry(std::function<void(::io_uring_sqe *handle)> prepare)
    {
        if (::io_uring_sqe *result = ::io_uring_get_sqe(&m_data); !result)
        {
            throw std::runtime_error("unable to get available SQE");
        }
        else
        {
            Entry entry;
            prepare(result);
            entry.finalize(result);
            return entry;
        }
    }

    ::io_uring &data()
    {
        return m_data;
    }

    void submit(unsigned total)
    {
        unsigned submitted = 0;
        do
        {
            if (int result = ::io_uring_submit(&m_data); result < 0)
            {
                throw std::system_error(-result, std::generic_category(), std::string(std::strerror(-result)));
            }
            else
            {
                assert(result <= std::numeric_limits<int>::max());
                submitted += static_cast<unsigned>(result);
            }
        } while (submitted < total);
    }

private:
    Context() = delete;
    Context(const Context &) = delete;

    ::io_uring m_data = {};

};

bool Context::Executor::exec(Context &context)
{
    std::size_t complete = 0;
    bool success = false;
    ::io_uring_cqe *response = nullptr;

    do
    {
        ::__kernel_timespec timeout
        {
            .tv_sec = 0,
            .tv_nsec = 100000,
        };

        if (int result = ::io_uring_wait_cqe_timeout(&context.data(), &response, &timeout); result < 0)
        {
            if ((-result) == ETIME)
            {
                // Log() << "timeout";
            }
            else
            {
                const std::string errorString(std::strerror(-result));
                for (auto i : m_entries)
                {
                    wake(*i);
                }
                complete = m_entries.size();
                Log() << "failed to wait for CQE, reason" << errorString;
            }
            ::io_uring_cqe_seen(&context.data(), response);
        }
        else
        {
            Log() << "wait" << result;
            if (auto iter(std::find_if(m_entries.begin(), m_entries.end(), [&response](const Entry *entry) -> bool
                {
                    return reinterpret_cast<std::uint64_t>(entry) == response->user_data;
                })); iter != m_entries.end())
            {
                wake(**iter);
                complete++;
                success = complete == m_entries.size();
            }
            else
            {
                Log() << "internal error: unknown entry" << reinterpret_cast<void*>(response->user_data);
                for (auto i : m_entries)
                {
                    wake(*i);
                }
                complete = m_entries.size();
            }
            ::io_uring_cqe_seen(&context.data(), response);
        }
    } while (complete < m_entries.size());

    return success;
}
    
}

int main_test();

int main([[maybe_unused]] int argc,[[maybe_unused]] char **argv)
{
    return main_test();
}

int main_test()
{
    const std::string filename(std::filesystem::temp_directory_path().string() + "/liburing-test.txt");
    {
        std::ofstream stream;
        Log() << filename;
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream.open(filename);
        
        stream << "io_uring test file";
    }

    FileDescriptor file(filename, FileDescriptor::Mode::Read);
    // FileDescriptor file(filename, FileDescriptor::Mode::Write);

    uring::Context::Parameters parameters;
    uring::Context context(parameters);

    constexpr auto offset = std::numeric_limits<std::uint64_t>::max();

    std::array<char, sizeof("io_uring ") - 1> buffer1;
    auto readRequest1(context.prepareEntry([&file, &buffer = buffer1](::io_uring_sqe *handle)
    {
        ::io_uring_prep_read(handle, file.handle(), buffer.data(), static_cast<unsigned>(buffer.size()), offset);
    }));
    // auto writeRequest1(context.prepareEntry([&file, &buffer = buffer1](const uring::Context::Entry &entry)
    // {
    //     ::io_uring_prep_write(entry.handle(), file.handle(), buffer.data(), static_cast<unsigned>(buffer.size()), offset);
    // }));

    std::array<char, sizeof("test ") - 1> buffer2;
    auto readRequest2(context.prepareEntry([&file, &buffer = buffer2](::io_uring_sqe *handle)
    {
        ::io_uring_prep_read(handle, file.handle(), buffer.data(), static_cast<unsigned>(buffer.size()), offset);
    }));
    // auto writeRequest2(context.prepareEntry([&file, &buffer = buffer2](const uring::Context::Entry &entry)
    // {
    //     ::io_uring_prep_write(entry.handle(), file.handle(), buffer.data(), static_cast<unsigned>(buffer.size()), offset);
    // }));

    std::array<char, sizeof("file") - 1> buffer3;
    auto readRequest3(context.prepareEntry([&file, &buffer = buffer3](::io_uring_sqe *handle)
    {
        ::io_uring_prep_read(handle, file.handle(), buffer.data(), static_cast<unsigned>(buffer.size()), offset);
    }));
    // auto writeRequest3(context.prepareEntry([&file, &buffer = buffer3](const uring::Context::Entry &entry)
    // {
    //     ::io_uring_prep_write(entry.handle(), file.handle(), buffer.data(), static_cast<unsigned>(buffer.size()), offset);
    // }));

    context.submit(3);

    // uring::Context::Executor executor({&writeRequest1, &writeRequest2, &writeRequest3});
    uring::Context::Executor executor({&readRequest1, &readRequest2, &readRequest3});
    // uring::Context::Executor executor({&readRequest1});

    auto executionResult(std::async([&executor, &context]() -> bool
    {
        return executor.exec(context);
    }));

    readRequest3.wait();
    readRequest1.wait();
    readRequest2.wait();

    // writeRequest3.wait();
    // writeRequest1.wait();
    // writeRequest2.wait();

    assert(executionResult.get());

    Log() << std::string(buffer1.data(), buffer1.size());
    Log() << std::string(buffer2.data(), buffer2.size());
    Log() << std::string(buffer3.data(), buffer3.size());

    Log() << "exiting";

    return 0;
}
