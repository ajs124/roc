Continuous integration
**********************

Overview
========

Travis is configured to build ``master`` and ``develop`` branches and pull requests.

Travis builds Roc for Linux and MacOS. Linux worker uses Docker to run builds on several Linux distros. Linux worker also uses QEMU to run cross-compiled tests.

Docker images for continuous integration and cross-compilation are prepared using Docker Hub automated builds. They are based on official upstream images, adding pre-installed packages required for build. Dockerfiles for images are hosted in a separate GitHub repository. When the Dockerfile or the upstream image changes, Docker Hub automatically triggers rebuild.

Links:
 * `Travis project <https://travis-ci.org/roc-project/roc>`_
 * `Travis configuration <https://github.com/roc-project/roc/blob/master/.travis.yml>`_
 * `Docker Hub organization <https://hub.docker.com/u/rocproject/>`_
 * `Dockerfiles repo <https://github.com/roc-project/dockerfiles>`_

Travis images
=============

======================================================== ============= ============= ===========================
Image                                                    Base image    Architecture  Compilers
======================================================== ============= ============= ===========================
rocproject/travis-ubuntu:18.04                           ubuntu:18.04  x86_64        gcc-6, clang-6
rocproject/travis-ubuntu:16.04                           ubuntu:16.04  x86_64        gcc-4.8, gcc-5, clang-3.7
rocproject/travis-ubuntu:14.04                           ubuntu:14.04  x86_64        gcc-4.4, gcc-4.6, clang-3.4
rocproject/travis-debian                                 debian:stable x86_64        distro default
rocproject/travis-fedora                                 fedora:latest x86_64        distro default
rocproject/travis-centos                                 centos:latest x86_64        distro default
rocproject/travis-min                                    ubuntu:latest x86_64        distro default
rocproject/cross-arm-bcm2708hardfp-linux-gnueabi:gcc-4.7 debian:stable armv6         gcc-4.7
rocproject/cross-arm-linux-gnueabihf:gcc-4.9             debian:stable armv7         gcc-4.9
rocproject/cross-aarch64-linux-gnu:gcc-7.4               debian:stable armv8         gcc-7.4
======================================================== ============= ============= ===========================

Run locally
===========

It is possible to run Linux builds locally, in the same environment as they are run on Travis.

For example, this will run Fedora build:

.. code::

    $ docker run -t --rm --cap-add SYS_PTRACE -u "${UID}" -v "${PWD}:${PWD}" -w "${PWD}" \
        rocproject/travis-fedora \
          scons --build-3rdparty=openfec,cpputest --enable-debug test

Explanation:

* ``-t`` allocates a pseudo-TTY to enable color output
* ``--rm`` removes the container when the command exits
* ``--cap-add SYS_PTRACE`` enables ptracing which is needed for clang sanitizers
* ``-u "${UID}"`` changes the UID inside the container from root to the current user
* ``-v "${PWD}:${PWD}"`` mounts the current directory into the container at the same path
* ``-w "${PWD}"`` chdirs into that directory
