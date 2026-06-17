#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>
#include <cstddef>
#include <cerrno>

// POSIX очередь сообщений: ядро хранит сообщения С ГРАНИЦАМИ (в отличие от
// потока байтов pipe/TCP) и с ПРИОРИТЕТАМИ — при чтении первым приходит
// сообщение с наибольшим приоритетом. Удобно для команд/задач между процессами.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//   (mq_* есть в libc на современной glibc; -lrt не нужен)

static constexpr long MQ_MAXMSG  = 10;
static constexpr long MQ_MSGSIZE = 256;

// Создать/открыть очередь name на чтение и запись с атрибутами
// (maxmsg=MQ_MAXMSG, msgsize=MQ_MSGSIZE). Вернуть mqd или (mqd_t)-1.
// Скелет:
//   struct mq_attr attr{}; attr.mq_maxmsg = MQ_MAXMSG; attr.mq_msgsize = MQ_MSGSIZE;
//   return mq_open(name, O_CREAT | O_RDWR, 0600, &attr);
mqd_t mq_setup(const char* name) {
    (void)name;
    return (mqd_t)-1; // TODO
}

// Отправить строку s с приоритетом prio. Вернуть 0/-1.
//   return mq_send(q, s, strlen(s), prio) == 0 ? 0 : -1;
int mq_send_msg(mqd_t q, const char* s, unsigned prio) {
    (void)q; (void)s; (void)prio;
    return -1; // TODO
}

// Принять сообщение в buf (размер buflen ДОЛЖЕН быть >= MQ_MSGSIZE), записать
// приоритет в *prio. Вернуть число байт (>=0) или -1.
//   return mq_receive(q, buf, buflen, prio);
ssize_t mq_recv_msg(mqd_t q, char* buf, size_t buflen, unsigned* prio) {
    (void)q; (void)buf; (void)buflen; (void)prio;
    return -1; // TODO
}
