# Stage 1: Build Environment
FROM ubuntu:22.04 AS builder

# Prevent tzdata prompts
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-update && apt-get install -y \
    build-essential \
    cmake \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# Build the project
RUN mkdir -p build && cd build && \
    cmake .. && \
    make -j$(nproc)

# Stage 2: Minimal Runtime Environment
FROM ubuntu:22.04

WORKDIR /app

# Copy the compiled binary from the builder stage
COPY --from=builder /app/build/redis_kv_store /app/redis_kv_store

# Expose default port
EXPOSE 6379

# Entry point starts the standalone server
ENTRYPOINT ["/app/redis_kv_store"]
CMD ["--port", "6379"]
