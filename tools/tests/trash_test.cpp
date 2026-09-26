// trash_test -- host test of user/trash.h + user/fsutil.h against mock_kapi.h (the kernel's
// return conventions). Build + run: tools/tests/run_trash_test.sh
#include "kapi.h"			// = mock_kapi.h (see run_trash_test.sh)
#include "trash.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main ()
{
	system ("rm -rf /tmp/onyx_mock && mkdir -p /tmp/onyx_mock/docs/sub && echo hi > /tmp/onyx_mock/docs/a.txt && echo x > /tmp/onyx_mock/docs/sub/b.txt");
	CHECK (fs_exists ("SD:/docs/a.txt"));
	CHECK (trash_move ("SD:/docs/a.txt"));			// true on success now
	CHECK (!fs_exists ("SD:/docs/a.txt"));
	CHECK (trash_count () == 1);
	char orig[256];
	CHECK (trash_origin ("a.txt", orig, sizeof orig) && !strcmp (orig, "SD:/docs/a.txt"));	// info written
	char where[256];
	CHECK (trash_restore ("a.txt", where, sizeof where));
	CHECK (fs_exists ("SD:/docs/a.txt") && !strcmp (where, "SD:/docs/a.txt"));
	CHECK (trash_count () == 0);
	CHECK (trash_move ("SD:/docs/sub"));			// a folder
	CHECK (trash_purge ("sub") && trash_count () == 0);
	CHECK (fs_copy_tree ("SD:/docs", "SD:/docs2", 0) && fs_exists ("SD:/docs2/a.txt"));
	CHECK (fs_remove_tree ("SD:/docs2", 0) && !fs_exists ("SD:/docs2"));
	printf (fails ? "%d FAILURE(S)\n" : "trash/fsutil: all checks passed\n", fails);
	return fails != 0;
}
