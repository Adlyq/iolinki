# iolinki CI Environment (Linux + Quality)
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    libcmocka-dev \
    cppcheck \
    pkg-config \
    git \
    python3 \
    python3-pip \
    doxygen \
    graphviz \
    && rm -rf /var/lib/apt/lists/*

# Pinned to match .pre-commit-config.yaml; check_quality.sh enforces this version.
RUN pip3 install --no-cache-dir clang-format==23.1.1

WORKDIR /workspace

COPY . .

RUN chmod +x test_all.sh check_quality.sh

# Default command: quality gate, then the host validation suite.
CMD ["/bin/bash", "-c", "./check_quality.sh && ./test_all.sh"]
