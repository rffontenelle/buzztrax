/* $Id: bt-cmd.c,v 1.10 2004-07-07 15:39:03 ensonic Exp $
 * You can try to run the uninstalled program via
 *   libtool --mode=execute bt-cmd <filename>
 * to enable debug output add:
 *  --gst-debug="*:2,bt-*:3" for not-so-much-logdata or
 *  --gst-debug="*:2,bt-*:4" for a-lot-logdata
 *
 * example songs can be found in ./test/songs/
 */

#define BT_CMD_C

#include "bt-cmd.h"

int main(int argc, char **argv) {
	gboolean res;
	BtCmdApplication *app;

	// init buzztard core
	bt_init(&argc,&argv);
	GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "bt-cmd", 0, "music production environment / command ui");
	
	app=(BtCmdApplication *)g_object_new(BT_TYPE_CMD_APPLICATION,NULL);

	res=bt_cmd_application_run(app,argc,argv);
	
	/* free application */
	g_object_unref(G_OBJECT(app));
	
	return(!res);
}
