FROM ubuntu:26.04
LABEL org.opencontainers.image.authors="support@halon.io"

ARG TARGETARCH
ARG TARGETOS

ARG HALON_REPO_USER
ENV HALON_REPO_USER=$HALON_REPO_USER
ARG HALON_REPO_PASS
ENV HALON_REPO_PASS=$HALON_REPO_PASS

RUN apt-get update && apt-get install -y wget gnupg
RUN apt-get update && apt-get install -y apt-transport-https

RUN wget -qO- https://raw.githubusercontent.com/halon/pgp-keys/master/pgp-keys/7F0A73B5.gpg > /usr/share/keyrings/halon-archive-keyring.gpg
RUN echo "deb [arch=${TARGETARCH} signed-by=/usr/share/keyrings/halon-archive-keyring.gpg] https://repo.halon.io/ resolute stable" > /etc/apt/sources.list.d/halon.list
RUN echo "machine repo.halon.io login ${HALON_REPO_USER} password ${HALON_REPO_PASS}" >> /etc/apt/auth.conf
RUN apt-get update && apt-get install -y halon-smtpd=6.11.3
RUN /usr/bin/install -d /var/run/halon

RUN apt-get install -y build-essential cmake git
RUN echo -n "UBU2604" > /OSRELEASE.txt

COPY entrypoint.sh /entrypoint.sh
RUN chmod 755 /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]

RUN apt-get update && apt-get install -y curl libssl-dev libz-dev
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

RUN apt-get install -y supervisor
COPY ./supervisord.conf /etc/supervisor/conf.d/supervisord.conf
CMD ["/usr/bin/supervisord"]