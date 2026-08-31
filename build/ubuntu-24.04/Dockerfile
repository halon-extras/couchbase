FROM ubuntu:24.04
LABEL org.opencontainers.image.authors="support@halon.io"

ARG TARGETARCH
ARG TARGETOS

ARG HALON_REPO_USER
ARG HALON_REPO_PASS

RUN apt-get update

RUN apt-get install -y wget gnupg
RUN apt-get install -y apt-transport-https

RUN wget -qO - https://raw.githubusercontent.com/halon/pgp-keys/master/pgp-keys/7F0A73B5.gpg | tee /usr/share/keyrings/halon-archive-keyring.gpg > /dev/null
RUN echo "deb [arch=`dpkg --print-architecture` signed-by=/usr/share/keyrings/halon-archive-keyring.gpg] https://repo.halon.io/ noble stable" >> /etc/apt/sources.list.d/halon.list
RUN echo "machine repo.halon.io login ${HALON_REPO_USER} password ${HALON_REPO_PASS}" >> /etc/apt/auth.conf
RUN apt-get update && apt-get install -y halon=6.3.0

RUN apt-get install -y build-essential cmake file
RUN echo -n "UBU2404" > /OSRELEASE.txt

RUN apt-get update && apt-get install -y curl libssl-dev libz-dev git
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
