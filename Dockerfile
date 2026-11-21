
FROM gcc:14

RUN apt-get update && apt-get install -y \
    libpq-dev \
    libssl-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY backend ./backend

RUN gcc backend/src/main.c \
    backend/src/database.c \
    backend/src/auth.c \
    backend/src/jwt.c \
    backend/src/project.c \
    backend/src/issue.c \
    -Ibackend/include \
    -o server \
    $(pkg-config --cflags --libs libpq openssl)

CMD ["./server"]
