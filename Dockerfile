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

# Pinned to match .pre-commit-config.yaml; tools/gates.sh enforces this version.
RUN pip3 install --no-cache-dir clang-format==23.1.1

WORKDIR /workspace

COPY . .

RUN chmod +x test_all.sh tools/gates.sh

# Default command: quality gate, then the host validation suite. The image has
# no Arm toolchain or gcovr, so the matrix/perf groups stay out of it.
CMD ["/bin/bash", "-c", "./tools/gates.sh quick build static docs package && ./test_all.sh"]
