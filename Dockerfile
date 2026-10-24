# iolinki CI Environment (Linux + Quality)
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    libcmocka-dev \
    clang-format \
    cppcheck \
    pkg-config \
    git \
    python3 \
    python3-pip \
    doxygen \
    graphviz \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY . .

RUN chmod +x test_all.sh check_quality.sh

# Default command: quality gate, then the host validation suite.
CMD ["/bin/bash", "-c", "./check_quality.sh && ./test_all.sh"]
