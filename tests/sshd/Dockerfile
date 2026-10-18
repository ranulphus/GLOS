# The OpenSSH server the sshc suite logs into (tests/loopa/jobs.py sshc; M10 step D): Debian's openssh-server, vi
# (vim-tiny), top (procps). A user "glos" whose password is the suite's; the host key, the authorized keys and the
# configuration are mounted at /keys by each run.
FROM python:3.12-bookworm
RUN apt-get update && apt-get install -y --no-install-recommends openssh-server vim-tiny procps \
    && rm -rf /var/lib/apt/lists/* && mkdir -p /run/sshd \
    && useradd -m -s /bin/bash glos && echo 'glos:glos-test-pw' | chpasswd
CMD ["/usr/sbin/sshd", "-D", "-e", "-f", "/keys/sshd_config"]
