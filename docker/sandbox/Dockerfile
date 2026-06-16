FROM ubuntu:22.04

# Установка компиляторов и инструментов отладки
RUN apt-get update && apt-get install -y \
    gcc \
    clang \
    make \
    libasan6 \
    libubsan1 \
    valgrind \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /sandbox

# Ограничения: нет сети, нет записи за пределы /sandbox
VOLUME ["/sandbox"]
