# Linux Your Way build environment.
# An Artix container with artools, used to build the [lyw] package repo and
# then the live ISO. Must be run with --privileged (loop devices, overlayfs,
# squashfs) — see build-iso.sh. Recipe adapted from Wheatley Linux.
FROM artixlinux/artixlinux:latest

# pacman's scriptlet sandbox needs network/namespace isolation that an
# unprivileged `docker build` can't grant (landlock/unshare -> EPERM).
# Turn it off for the build image; the resulting ISO is unaffected.
RUN sed -i '/^\[options\]/a DisableSandbox' /etc/pacman.conf

# base toolchain + artools (buildiso) + makepkg deps, in one coherent upgrade
RUN pacman -Syu --noconfirm --needed \
        base-devel git sudo \
        artools artools-base \
        squashfs-tools dosfstools libisoburn \
        gptfdisk parted \
        perl && \
    pacman -Scc --noconfirm

RUN pacman-key --init && pacman-key --populate artix

# unprivileged build user (makepkg refuses to run as root)
RUN useradd -m -G wheel builder && \
    echo 'builder ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/builder

WORKDIR /lyw
COPY . /lyw
RUN chown -R builder:builder /lyw

ENTRYPOINT ["/lyw/scripts/make-iso.sh"]
