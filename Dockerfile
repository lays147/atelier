FROM base/archlinux

WORKDIR /home
RUN mkdir atcore atelier
ENV core /home/atcore
ENV ui /home/atelier
#Setup
RUN pacman -Syu 
RUN pacman -S \
gcc \
git \
qt5-base \
qt5-serialport \ 
qt5-charts \
qt5-3d \
qt5-multimedia \
qwt \
kxmlgui \
solid \
kconfigwidgets \
ktexteditor \
ki18n \
make \
cmake extra-cmake-modules --noconfirm && \
cd $core && git clone https://github.com/KDE/atcore.git && \
cmake atcore && make install && \
cd $ui && git clone https://github.com/KDE/atelier.git && \
cmake atelier && make install

CMD atelier
