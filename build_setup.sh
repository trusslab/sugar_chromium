cd build/linux/debian_wheezy_amd64-sysroot/usr/include/x86_64-linux-gnu

ln -s /usr/lib/x86_64-linux-gnu/include/xf86drm2.h .
ln -s /usr/lib/x86_64-linux-gnu/include/libdrm2 .
ln -s /usr/lib/x86_64-linux-gnu/include/xf86drm2Mode.h .
ln -s /usr/include/sugar .
ln -s /usr/lib/x86_64-linux-gnu/include/gbm.h gbm2.h

cd ../GL
ln -s /usr/include/GL/glu.h
ln -s /usr/include/GL/glut.h
ln -s /usr/include/GL/freeglut_std.h

cd ../
sed -i '40s/.*/\#include\ \<libdrm\/drm\.h\>/' xf86drm.h

cd ../../../../../