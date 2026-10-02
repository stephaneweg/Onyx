/*
 * third_party/dav1d-1.5.1/onyx/onyx_sysconf.c -- dav1d's processor count on the Pi (newlib has
 * no sysconf; config.h renames dav1d's call to this). One: user/av decodes on one thread.
 */
long onyx_dav1d_sysconf(int name);
long onyx_dav1d_sysconf(int name)
{
	(void) name;
	return 1;
}
