FROM mcr.microsoft.com/azurelinux/base/core:3.0
LABEL org.opencontainers.image.authors="support@halon.io"

ARG TARGETARCH
ARG TARGETOS

ARG HALON_REPO_USER
ARG HALON_REPO_PASS

RUN tdnf install -y ca-certificates azurelinux-repos-extended

RUN echo "[halon]" >> /etc/yum.repos.d/Halon.repo
RUN echo "name=Halon repository" >> /etc/yum.repos.d/Halon.repo
RUN echo "baseurl=https://${HALON_REPO_USER}:${HALON_REPO_PASS}@repo.halon.io/dists/azure3/${TARGETARCH}/stable" >> /etc/yum.repos.d/Halon.repo
RUN echo "gpgcheck=1" >> /etc/yum.repos.d/Halon.repo
RUN echo "gpgkey=https://raw.githubusercontent.com/halon/changelog/master/pgp-keys/7F0A73B5.asc" >> /etc/yum.repos.d/Halon.repo
RUN echo "enabled=1" >> /etc/yum.repos.d/Halon.repo
RUN tdnf install -y halon-6.8.0

RUN tdnf install -y cmake make build-essential rpm-build
RUN echo -n "AZURE3" > /OSRELEASE.txt

RUN tdnf install -y openssl-devel git
RUN curl -fsSL \
    "https://github.com/couchbase/couchbase-cxx-client/archive/refs/tags/1.4.0.tar.gz" \
    -o /tmp/couchbase-cxx.tar.gz \
    && tar -xzf /tmp/couchbase-cxx.tar.gz -C /tmp \
    && cmake \
        -S "/tmp/couchbase-cxx-client-1.4.0" \
        -B /tmp/couchbase-cxx-build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCOUCHBASE_CXX_CLIENT_INSTALL=ON \
        -DCOUCHBASE_CXX_CLIENT_BUILD_STATIC=ON \
        -DCOUCHBASE_CXX_CLIENT_BUILD_SHARED=OFF \
        -DCOUCHBASE_CXX_CLIENT_BUILD_COUCHBASE2=OFF \
        -DCOUCHBASE_CXX_CLIENT_BUILD_TOOLS=OFF \
        -DCOUCHBASE_CXX_CLIENT_BUILD_OPENTELEMETRY=OFF \
        -DCOUCHBASE_CXX_CLIENT_BUILD_TESTS=OFF \
        -DCOUCHBASE_CXX_CLIENT_BUILD_EXAMPLES=OFF \
        -DCOUCHBASE_CXX_CLIENT_EMBED_MOZILLA_CA_BUNDLE=OFF \
    && cmake --build /tmp/couchbase-cxx-build -j"$(nproc)" \
    && cmake --install /tmp/couchbase-cxx-build

COPY build.sh /build.sh
CMD ["/build.sh"]
