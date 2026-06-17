#include <sys/inotify.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <set>

// inotify — наблюдение за файловой системой через дескриптор. Здесь ты
// поддерживаешь точную МОДЕЛЬ содержимого каталога (множество имён файлов),
// разбирая поток событий create / delete / rename. Тонкость — переименование
// внутри каталога приходит ДВУМЯ событиями: IN_MOVED_FROM (старое имя) +
// IN_MOVED_TO (новое). И события переменной длины надо корректно парсить.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog

class DirWatch {
public:
    // Создать inotify (IN_NONBLOCK | IN_CLOEXEC) и добавить watch на каталог path
    // с маской IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO.
    // Вернуть 0/-1.
    int init(const char* path) {
        (void)path;
        return -1; // TODO
    }

    // Подождать события до timeout_ms (poll на inotify-fd), затем вычитать ВСЕ
    // доступные события и обновить модель:
    //   IN_CREATE / IN_MOVED_TO   → добавить имя в model_
    //   IN_DELETE / IN_MOVED_FROM → убрать имя из model_
    // Парсинг: события идут подряд в буфере, каждое — struct inotify_event + name
    // длиной e->len; шаг = sizeof(inotify_event) + e->len. Читать до EAGAIN.
    void process(int timeout_ms) {
        (void)timeout_ms;
        // TODO
    }

    int  contains(const char* name) const { return model_.count(name) ? 1 : 0; }
    int  count() const                    { return (int)model_.size(); }

    ~DirWatch() { if (fd_ >= 0) close(fd_); }

private:
    int fd_ = -1;
    std::set<std::string> model_;
};

extern "C" {
    DirWatch* dw_create()                              { return new DirWatch(); }
    void dw_destroy(DirWatch* w)                       { delete w; }
    int  dw_init(DirWatch* w, const char* path)        { return w->init(path); }
    void dw_process(DirWatch* w, int timeout_ms)       { w->process(timeout_ms); }
    int  dw_contains(DirWatch* w, const char* name)    { return w->contains(name); }
    int  dw_count(DirWatch* w)                         { return w->count(); }
}
