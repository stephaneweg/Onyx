/* config.h -- Pocket alpha stt_circle : configuration de PocketSphinx 5.1.1 pour Circle
 * (et hosttest). Repris de stt_essai/build/ps/config.h (cmake) sans popen, unistd.h,
 * sys/stat.h ni PS_USE_THREAD_LOCAL_RNG (pas de TLS sous Circle). */

/* Default radix point for fixed-point */
#define DEFAULT_RADIX 12

/* Define to use fixed-point computation */
/* #undef FIXED_POINT */

/* Define to use thread-local storage for random number generator */
/* #undef PS_USE_THREAD_LOCAL_RNG */

/* Define if the system has the type `long long'. */
#define HAVE_LONG_LONG

/* Define if you have the `snprintf' function. */
#define HAVE_SNPRINTF

/* Define if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H

/* Define if you have the <stdint.h> header file. */
#define HAVE_STDINT_H

/* The size of `long', as computed by sizeof. */
#define SIZEOF_LONG 8

/* The size of `long long', as computed by sizeof. */
#define SIZEOF_LONG_LONG 8

/* Define WORDS_BIGENDIAN if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN
#endif

#define PACKAGE_BUGREPORT "dhdaines@gmail.com"
#define PACKAGE_NAME "PocketSphinx"
#define PACKAGE_STRING "PocketSphinx 5.1.1"
#define PACKAGE_TARNAME "pocketsphinx-5.1.1"
#define PACKAGE_URL "https://github.com/cmusphinx/pocketsphinx"
#define PACKAGE_VERSION "5.1.1"
