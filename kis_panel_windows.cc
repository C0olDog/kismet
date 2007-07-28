/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "config.h"

// Panel has to be here to pass configure, so just test these
#if (defined(HAVE_LIBNCURSES) || defined (HAVE_LIBCURSES))

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#include "kis_panel_widgets.h"
#include "kis_panel_frontend.h"
#include "kis_panel_windows.h"
#include "kis_panel_preferences.h"

Kis_Main_Panel::Kis_Main_Panel(GlobalRegistry *in_globalreg, 
							   KisPanelInterface *in_intf) : 
	Kis_Panel(in_globalreg, in_intf) {

	menu = new Kis_Menu(globalreg, this);

	mn_file = menu->AddMenu("Kismet", 0);
	mi_connect = menu->AddMenuItem("Connect...", mn_file, 'C');
	mi_disconnect = menu->AddMenuItem("Disconnect", mn_file, 'D');
	menu->AddMenuItem("-", mn_file, 0);
	mi_quit = menu->AddMenuItem("Quit", mn_file, 'Q');

	menu->EnableMenuItem(mi_connect);
	menu->DisableMenuItem(mi_disconnect);
	connect_enable = 1;

	mn_sort = menu->AddMenu("Sort", 0);
	mi_sort_auto = menu->AddMenuItem("Auto-fit", mn_sort, 'a');
	menu->AddMenuItem("-", mn_sort, 0);
	mi_sort_type = menu->AddMenuItem("Type", mn_sort, 't');
	mi_sort_chan = menu->AddMenuItem("Channel", mn_sort, 'c');
	mi_sort_first = menu->AddMenuItem("First Seen", mn_sort, 'f');
	mi_sort_first_d = menu->AddMenuItem("First Seen (descending)", mn_sort, 'F');
	mi_sort_last = menu->AddMenuItem("Latest Seen", mn_sort, 'l');
	mi_sort_last_d = menu->AddMenuItem("Latest Seen (descending)", mn_sort, 'L');
	mi_sort_bssid = menu->AddMenuItem("BSSID", mn_sort, 'b');
	mi_sort_ssid = menu->AddMenuItem("SSID", mn_sort, 's');
	mi_sort_packets = menu->AddMenuItem("Packets", mn_sort, 'p');
	mi_sort_packets_d = menu->AddMenuItem("Packets (descending)", mn_sort, 'P');

	mn_tools = menu->AddMenu("Tools", 0);
	mi_addcard = menu->AddMenuItem("Add Source...", mn_tools, 'A');

	menu->AddMenuItem("-", mn_tools, 0);

	mn_plugins = menu->AddSubMenuItem("Plugins", mn_tools, 'x');
	mi_addplugin = menu->AddMenuItem("Add Plugin...", mn_plugins, 'P');
	menu->AddMenuItem("-", mn_plugins, 0);
	mi_noplugins = menu->AddMenuItem("No plugins available...", mn_plugins, 0);
	menu->DisableMenuItem(mi_noplugins);

	mn_preferences = menu->AddSubMenuItem("Preferences", mn_tools, 'P');
	mi_colorprefs = menu->AddMenuItem("Colors...", mn_preferences, 'C');

	menu->Show();

	// Make a hbox to hold the network list and additional info widgets,
	// and the vertical stack of optional widgets
	hbox = new Kis_Panel_Packbox(globalreg, this);
	hbox->SetPackH();
	hbox->SetHomogenous(0);
	hbox->SetSpacing(0);
	hbox->Show();

	// Make a vbox to hold the hbox we just made, and the status text
	vbox = new Kis_Panel_Packbox(globalreg, this);
	vbox->SetPackV();
	vbox->SetHomogenous(0);
	vbox->SetSpacing(0);
	vbox->Show();

	// Make the network pack box which holds the network widget and the 
	// extra info line widget
	netbox = new Kis_Panel_Packbox(globalreg, this);
	netbox->SetPackV();
	netbox->SetSpacing(0);
	netbox->SetHomogenous(0);
	netbox->SetName("KIS_MAIN_NETBOX");
	netbox->Show();

	// Make the one-line horizontal box which holds GPS, battery, etc
	linebox = new Kis_Panel_Packbox(globalreg, this);
	linebox->SetPackH();
	linebox->SetSpacing(1);
	linebox->SetHomogenous(0);
	linebox->SetName("KIS_MAIN_LINEBOX");
	linebox->SetPreferredSize(0, 1);
	linebox->Show();

	// Make the vertical box holding things like the # of networks
	optbox = new Kis_Panel_Packbox(globalreg, this);
	optbox->SetPackV();
	optbox->SetSpacing(1);
	optbox->SetHomogenous(0);
	optbox->SetName("KIS_MAIN_OPTBOX");
	optbox->SetPreferredSize(10, 0);
	optbox->Show();

	statustext = new Kis_Status_Text(globalreg, this);
	statuscli = new KisStatusText_Messageclient(globalreg, statustext);
	globalreg->messagebus->RegisterClient(statuscli, MSGFLAG_ALL);

	// We only want 5 lines of status text
	statustext->SetPreferredSize(0, 5);
	statustext->SetName("KIS_MAIN_STATUS");
	statustext->Show();

	netlist = new Kis_Netlist(globalreg, this);
	netlist->SetName("KIS_MAIN_NETLIST");
	netlist->Show();

	// Pack our boxes together
	hbox->Pack_End(netbox, 1, 0);
	hbox->Pack_End(optbox, 0, 0);

	netbox->Pack_End(netlist, 1, 0);
	netbox->Pack_End(linebox, 0, 0);

	vbox->Pack_End(hbox, 1, 0);
	vbox->Pack_End(statustext, 0, 0);

	active_component = netlist;

	comp_vec.push_back(vbox);

	if (kpinterface->prefs.FetchOpt("LOADEDFROMFILE") != "1") {
		_MSG("Failed to load preferences file, will use defaults", MSGFLAG_INFO);
	}
}

Kis_Main_Panel::~Kis_Main_Panel() {
	globalreg->messagebus->RemoveClient(statuscli);

}

void Kis_Main_Panel::Position(int in_sy, int in_sx, int in_y, int in_x) {
	Kis_Panel::Position(in_sy, in_sx, in_y, in_x);

	menu->SetPosition(1, 0, 0, 0);

	// All we have to do is position the main box now
	vbox->SetPosition(in_sx + 1, in_sy + 1, in_x - 1, in_y - 2);

	/*
	netlist->SetPosition(in_sx + 2, in_sy + 1, in_x - 15, in_y - 8);
	statustext->SetPosition(in_sx + 1, in_y - 7, in_x - 2, 5);
	*/
}

void Kis_Main_Panel::DrawPanel() {
	ColorFromPref(text_color, "panel_text_color");
	ColorFromPref(border_color, "panel_border_color");

	wbkgdset(win, text_color);
	werase(win);

	DrawTitleBorder();

	wattrset(win, text_color);
	for (unsigned int x = 0; x < comp_vec.size(); x++)
		comp_vec[x]->DrawComponent();

	UpdateSortMenu();

	menu->DrawComponent();

	wmove(win, 0, 0);
}

int Kis_Main_Panel::KeyPress(int in_key) {
	int ret;

	vector<KisNetClient *> *clivec = kpinterface->FetchNetClientVecPtr();

	if (clivec->size() == 0 && connect_enable == 0) {
		menu->EnableMenuItem(mi_connect);
		menu->DisableMenuItem(mi_disconnect);
		connect_enable = 1;
	} else if (clivec->size() > 0 && connect_enable) {
		menu->EnableMenuItem(mi_disconnect);
		menu->DisableMenuItem(mi_connect);
		connect_enable = 0;
	}
	
	// Give the menu first shot, it'll ignore the key if it didn't have 
	// anything open.
	ret = menu->KeyPress(in_key);

	if (ret > 0) {
		// Menu processed an event, do something with it
		if (ret == mi_quit) {
			return -1;
		} else if (ret == mi_connect) {
			Kis_Connect_Panel *cp = new Kis_Connect_Panel(globalreg, kpinterface);
			cp->Position((LINES / 2) - 4, (COLS / 2) - 20, 8, 40);
			kpinterface->AddPanel(cp);
		} else if (ret == mi_disconnect) {
			if (clivec->size() > 0) {
				kpinterface->RemoveNetClient((*clivec)[0]);
			}
		} else if (ret == mi_sort_auto) {
			kpinterface->prefs.SetOpt("NETLIST_SORT", "auto", 1);
		} else if (ret == mi_sort_type) {
			kpinterface->prefs.SetOpt("NETLIST_SORT", "type", 1);
		} else if (ret == mi_sort_chan) {
			kpinterface->prefs.SetOpt("NETLIST_SORT", "channel", 1);
		} else if (ret == mi_sort_first) {
			kpinterface->prefs.SetOpt("NETLIST_SORT", "first", 1);
		} else if (ret == mi_sort_first_d) {
			kpinterface->prefs.SetOpt("NETLIST_SORT", "first_desc", 1);
		} else if (ret == mi_sort_last) {
			kpinterface->prefs.SetOpt("NETLIST_SORT", "last", 1);
		} else if (ret == mi_sort_last_d) {
			kpinterface->prefs.SetOpt("NETLIST_SORT", "last_desc", 1);
		} else if (ret == mi_sort_bssid) {
			kpinterface->prefs.SetOpt("NETLIST_SORT", "bssid", 1);
		} else if (ret == mi_sort_ssid) {
			kpinterface->prefs.SetOpt("NETLIST_SORT", "ssid", 1);
		} else if (ret == mi_sort_packets) {
			kpinterface->prefs.SetOpt("NETLIST_SORT", "packets", 1);
		} else if (ret == mi_sort_packets_d) {
			kpinterface->prefs.SetOpt("NETLIST_SORT", "packets_desc", 1);
		} else if (ret == mi_addcard) {
			vector<KisNetClient *> *cliref = kpinterface->FetchNetClientVecPtr();
			if (cliref->size() == 0) {
				kpinterface->RaiseAlert("No servers",
										"There are no servers.  You must\n"
										"connect to a server before adding\n"
										"cards.\n");
			} else if (cliref->size() == 1) {
				sp_addcard_cb(globalreg, kpinterface, (*cliref)[0], NULL);
			} else {
				kpinterface->RaiseServerPicker("Choose server", sp_addcard_cb,
											   NULL);
			}

		} else if (ret == mi_addplugin) {
			Kis_Plugin_Picker *pp = new Kis_Plugin_Picker(globalreg, kpinterface);
			pp->Position((LINES / 2) - 8, (COLS / 2) - 20, 16, 50);
			kpinterface->AddPanel(pp);
		} else if (ret == mi_colorprefs) {
			SpawnColorPrefs();
		} else {
			for (unsigned int p = 0; p < plugin_menu_vec.size(); p++) {
				if (ret == plugin_menu_vec[p].menuitem) {
					(*(plugin_menu_vec[p].callback))(plugin_menu_vec[p].auxptr);
					break;
				}
			}
		}

		return 0;
	} else if (ret == -1) {
		return 0;
	}

	// Otherwise the menu didn't touch the key, so pass it to the top
	// component
	if (active_component != NULL) {
		ret = active_component->KeyPress(in_key);

		if (ret == 0)
			return 0;

		return ret;
	}

	return 0;
}

void Kis_Main_Panel::AddPluginMenuItem(string in_name, int (*callback)(void *),
									   void *auxptr) {
	plugin_menu_opt mo;

	// Hide the "no plugins" menu and make our own item
	menu->SetMenuItemVis(mi_noplugins, 0);
	mo.menuitem = menu->AddMenuItem(in_name, mn_plugins, 0);
	mo.callback = callback;
	mo.auxptr = auxptr;

	plugin_menu_vec.push_back(mo);
}

void Kis_Main_Panel::SpawnColorPrefs() {
	Kis_ColorPref_Panel *cpp = new Kis_ColorPref_Panel(globalreg, kpinterface);

	cpp->AddColorPref("panel_text_color", "Text");
	cpp->AddColorPref("panel_border_color", "Window Border");
	cpp->AddColorPref("menu_text_color", "Menu Text");
	cpp->AddColorPref("menu_border_color", "Menu Border");
	cpp->AddColorPref("netlist_header_color", "Netlist Header");
	cpp->AddColorPref("netlist_normal_color", "Netlist Normal");
	cpp->AddColorPref("netlist_crypt_color", "Netlist Encrypted");
	cpp->AddColorPref("netlist_group_color", "Netlist Group");
	cpp->AddColorPref("netlist_factory_color", "Netlist Factory");
	cpp->AddColorPref("status_normal_color", "Status Text");

	cpp->Position((LINES / 2) - 7, (COLS / 2) - 20, 14, 40);
	kpinterface->AddPanel(cpp);
}

Kis_Display_NetGroup *Kis_Main_Panel::FetchSelectedNetgroup() {
	if (netlist == NULL)
		return NULL;

	return netlist->FetchSelectedNetgroup();
}

vector<Kis_Display_NetGroup *> *Kis_Main_Panel::FetchDisplayNetgroupVector() {
	if (netlist == NULL)
		return NULL;

	return netlist->FetchDisplayVector();
}

void Kis_Main_Panel::UpdateSortMenu() {
	netsort_opts so = netlist->FetchSortMode();

	if (so == netsort_autofit)
		menu->SetMenuItemChecked(mi_sort_auto, 1);
	else
		menu->SetMenuItemChecked(mi_sort_auto, 0);

	if (so == netsort_type)
		menu->SetMenuItemChecked(mi_sort_type, 1);
	else
		menu->SetMenuItemChecked(mi_sort_type, 0);

	if (so == netsort_channel)
		menu->SetMenuItemChecked(mi_sort_chan, 1);
	else
		menu->SetMenuItemChecked(mi_sort_chan, 0);

	if (so == netsort_first)
		menu->SetMenuItemChecked(mi_sort_first, 1);
	else
		menu->SetMenuItemChecked(mi_sort_first, 0);

	if (so == netsort_first_desc)
		menu->SetMenuItemChecked(mi_sort_first_d, 1);
	else
		menu->SetMenuItemChecked(mi_sort_first_d, 0);

	if (so == netsort_last)
		menu->SetMenuItemChecked(mi_sort_last, 1);
	else
		menu->SetMenuItemChecked(mi_sort_last, 0);

	if (so == netsort_last_desc)
		menu->SetMenuItemChecked(mi_sort_last_d, 1);
	else
		menu->SetMenuItemChecked(mi_sort_last_d, 0);

	if (so == netsort_bssid)
		menu->SetMenuItemChecked(mi_sort_bssid, 1);
	else
		menu->SetMenuItemChecked(mi_sort_bssid, 0);

	if (so == netsort_ssid)
		menu->SetMenuItemChecked(mi_sort_ssid, 1);
	else
		menu->SetMenuItemChecked(mi_sort_ssid, 0);

	if (so == netsort_packets)
		menu->SetMenuItemChecked(mi_sort_packets, 1);
	else
		menu->SetMenuItemChecked(mi_sort_packets, 0);

	if (so == netsort_packets_desc)
		menu->SetMenuItemChecked(mi_sort_packets_d, 1);
	else
		menu->SetMenuItemChecked(mi_sort_packets_d, 0);
}

Kis_Connect_Panel::Kis_Connect_Panel(GlobalRegistry *in_globalreg, 
									 KisPanelInterface *in_intf) :
	Kis_Panel(in_globalreg, in_intf) {

	hostname = new Kis_Single_Input(globalreg, this);
	hostport = new Kis_Single_Input(globalreg, this);
	cancelbutton = new Kis_Button(globalreg, this);
	okbutton = new Kis_Button(globalreg, this);

	tab_components.push_back(hostname);
	tab_components.push_back(hostport);
	tab_components.push_back(okbutton);
	tab_components.push_back(cancelbutton);
	tab_pos = 0;

	active_component = hostname;

	SetTitle("Connect to Server");

	hostname->SetLabel("Host", LABEL_POS_LEFT);
	hostname->SetTextLen(120);
	hostname->SetCharFilter(FILTER_ALPHANUMSYM);

	hostport->SetLabel("Port", LABEL_POS_LEFT);
	hostport->SetTextLen(5);
	hostport->SetCharFilter(FILTER_NUM);

	okbutton->SetText("Connect");
	cancelbutton->SetText("Cancel");

	hostname->Show();
	hostport->Show();
	okbutton->Show();
	cancelbutton->Show();

	vbox = new Kis_Panel_Packbox(globalreg, this);
	vbox->SetPackV();
	vbox->SetHomogenous(0);
	vbox->SetSpacing(1);
	vbox->Show();

	bbox = new Kis_Panel_Packbox(globalreg, this);
	bbox->SetPackH();
	bbox->SetHomogenous(1);
	bbox->SetSpacing(1);
	bbox->SetCenter(1);
	bbox->Show();

	bbox->Pack_End(cancelbutton, 0, 0);
	bbox->Pack_End(okbutton, 0, 0);

	vbox->Pack_End(hostname, 0, 0);
	vbox->Pack_End(hostport, 0, 0);
	vbox->Pack_End(bbox, 1, 0);

	comp_vec.push_back(vbox);

	active_component = hostname;
	hostname->Activate(1);
}

Kis_Connect_Panel::~Kis_Connect_Panel() {
}

void Kis_Connect_Panel::Position(int in_sy, int in_sx, int in_y, int in_x) {
	Kis_Panel::Position(in_sy, in_sx, in_y, in_x);

	vbox->SetPosition(1, 2, in_x - 2, in_y - 3);
}

void Kis_Connect_Panel::DrawPanel() {
	ColorFromPref(text_color, "panel_text_color");
	ColorFromPref(border_color, "panel_border_color");

	wbkgdset(win, text_color);
	werase(win);

	DrawTitleBorder();

	wattrset(win, text_color);

	for (unsigned int x = 0; x < comp_vec.size(); x++)
		comp_vec[x]->DrawComponent();

	wmove(win, 0, 0);
}

int Kis_Connect_Panel::KeyPress(int in_key) {
	int ret;

	// Rotate through the tabbed items
	if (in_key == '\t') {
		tab_components[tab_pos]->Deactivate();
		tab_pos++;
		if (tab_pos >= (int) tab_components.size())
			tab_pos = 0;
		tab_components[tab_pos]->Activate(1);
		active_component = tab_components[tab_pos];
	}

	// Otherwise the menu didn't touch the key, so pass it to the top
	// component
	if (active_component != NULL) {
		ret = active_component->KeyPress(in_key);

		if (active_component == okbutton && ret == 1) {
			if (hostname->GetText() == "")  {
				kpinterface->RaiseAlert("No hostname",
										"No hostname was provided for creating a\n"
										"new client connect to a Kismet server.\n"
										"A valid host name or IP is required.\n");
				return(0);
			}

			if (hostport->GetText() == "")  {
				kpinterface->RaiseAlert("No port",
										"No port number was provided for creating a\n"
										"new client connect to a Kismet server.\n"
										"A valid port number is required.\n");
				return(0);
			}
			
			// Try to add a client
			string clitxt = "tcp://" + hostname->GetText() + ":" +
				hostport->GetText();

			if (kpinterface->AddNetClient(clitxt, 1) < 0) 
				kpinterface->RaiseAlert("Connect failed", 
										"Failed to create new client connection\n"
										"to a Kismet server.  Check the status\n"
										"pane for more information about what\n"
										"went wrong.\n");

			globalreg->panel_interface->KillPanel(this);
		} else if (active_component == cancelbutton && ret == 1) {
			// Cancel and close
			globalreg->panel_interface->KillPanel(this);
		}
	}

	return 0;
}

Kis_ModalAlert_Panel::Kis_ModalAlert_Panel(GlobalRegistry *in_globalreg, 
										   KisPanelInterface *in_intf) :
	Kis_Panel(in_globalreg, in_intf) {

	ftxt = new Kis_Free_Text(globalreg, this);
	ackbutton = new Kis_Button(globalreg, this);

	comp_vec.push_back(ftxt);
	comp_vec.push_back(ackbutton);

	tab_components.push_back(ackbutton);
	tab_pos = 0;

	active_component = ackbutton;

	SetTitle("");

	ackbutton->SetText("OK");
}

Kis_ModalAlert_Panel::~Kis_ModalAlert_Panel() {
}

void Kis_ModalAlert_Panel::Position(int in_sy, int in_sx, int in_y, int in_x) {
	Kis_Panel::Position(in_sy, in_sx, in_y, in_x);

	ftxt->SetPosition(1, 1, in_x - 2, in_y - 3);
	ackbutton->SetPosition((in_x / 2) - 7, in_y - 2, 14, 1);

	ackbutton->Activate(1);
	active_component = ackbutton;

	ftxt->Show();
	ackbutton->Show();
}

void Kis_ModalAlert_Panel::DrawPanel() {
	ColorFromPref(text_color, "panel_text_color");
	ColorFromPref(border_color, "panel_border_color");

	wbkgdset(win, text_color);
	werase(win);

	DrawTitleBorder();

	wattrset(win, text_color);
	DrawTitleBorder();

	for (unsigned int x = 0; x < comp_vec.size(); x++)
		comp_vec[x]->DrawComponent();

	wmove(win, 0, 0);
}

int Kis_ModalAlert_Panel::KeyPress(int in_key) {
	int ret;

	// Rotate through the tabbed items
	if (in_key == '\t') {
		tab_components[tab_pos]->Deactivate();
		tab_pos++;
		if (tab_pos >= (int) tab_components.size())
			tab_pos = 0;
		tab_components[tab_pos]->Activate(1);
		active_component = tab_components[tab_pos];
	}

	// Otherwise the menu didn't touch the key, so pass it to the top
	// component
	if (active_component != NULL) {
		ret = active_component->KeyPress(in_key);

		if (active_component == ackbutton && ret == 1) {
			// We're done
			globalreg->panel_interface->KillPanel(this);
		}
	}

	return 0;
}

void Kis_ModalAlert_Panel::ConfigureAlert(string in_title, string in_text) {
	SetTitle(in_title);
	ftxt->SetText(in_text);
}

Kis_ServerList_Picker::Kis_ServerList_Picker(GlobalRegistry *in_globalreg, 
											 KisPanelInterface *in_intf) :
	Kis_Panel(in_globalreg, in_intf) {

	// Grab the pointer to the list of clients maintained
	netcliref = kpinterface->FetchNetClientVecPtr();

	srvlist = new Kis_Scrollable_Table(globalreg, this);

	comp_vec.push_back(srvlist);

	// TODO -- Add name parsing to KISMET proto in netclient, add support here
	vector<Kis_Scrollable_Table::title_data> titles;
	Kis_Scrollable_Table::title_data t;
	t.width = 16;
	t.title = "Host";
	t.alignment = 0;
	titles.push_back(t);
	t.width = 5;
	t.title = "Port";
	t.alignment = 2;
	titles.push_back(t);
	t.width = 4;
	t.title = "Cntd";
	t.alignment = 0;
	titles.push_back(t);
	t.width = 3;
	t.title = "Rdy";
	t.alignment = 0;
	titles.push_back(t);
	srvlist->AddTitles(titles);

	// Population is done during draw

	active_component = srvlist;

	srvlist->Activate(1);

	SetTitle("");

	cb_hook = NULL;
	cb_aux = NULL;
}

Kis_ServerList_Picker::~Kis_ServerList_Picker() {
}

void Kis_ServerList_Picker::Position(int in_sy, int in_sx, int in_y, int in_x) {
	Kis_Panel::Position(in_sy, in_sx, in_y, in_x);

	srvlist->SetPosition(1, 1, in_x - 2, in_y - 2);

	srvlist->Show();
}

void Kis_ServerList_Picker::DrawPanel() {
	ColorFromPref(text_color, "panel_text_color");
	ColorFromPref(border_color, "panel_border_color");

	wbkgdset(win, text_color);
	werase(win);

	DrawTitleBorder();

	wattrset(win, text_color);

	DrawTitleBorder();

	// Grab the list of servers and populate with it.  We'll assume that the number
	// of servers, and their order, cannot change while we're in the picker, since
	// the user can't get at it.  We WILL have to handle updating the connection
	// status based on the position key.  This is NOT A SAFE ASSUMPTION for any other
	// of the picker types (like cards), so don't blind-copy this code later.
	vector<string> td;
	ostringstream osstr;
	for (unsigned int x = 0; x < netcliref->size(); x++) {
		td.clear();

		td.push_back((*netcliref)[x]->FetchHost());

		osstr << (*netcliref)[x]->FetchPort();
		td.push_back(osstr.str());
		osstr.str("");

		if ((*netcliref)[x]->Valid()) {
			td.push_back("Yes");
			if ((*netcliref)[x]->FetchConfigured() < 0)
				td.push_back("Tes");
			else
				td.push_back("No");
		} else {
			td.push_back("No");
			td.push_back("No");
		}

		srvlist->ReplaceRow(x, td);
	}


	for (unsigned int x = 0; x < comp_vec.size(); x++)
		comp_vec[x]->DrawComponent();

	wmove(win, 0, 0);
}

int Kis_ServerList_Picker::KeyPress(int in_key) {
	int ret;
	int listkey;
	
	// Rotate through the tabbed items
	if (in_key == '\n' || in_key == '\r') {
		listkey = srvlist->GetSelected();

		// Sanity check, even though nothing should be able to change this
		// while we're open since we claim the input.
		// We could raise an alert but theres nothing the user could do 
		// about it so we'll just silently close the window
		if (listkey >= 0 && listkey < (int) netcliref->size()) {
			(*cb_hook)(globalreg, kpinterface, (*netcliref)[listkey], cb_aux);
		}

		globalreg->panel_interface->KillPanel(this);
	}

	// Otherwise the menu didn't touch the key, so pass it to the top
	// component
	if (active_component != NULL) {
		ret = active_component->KeyPress(in_key);
	}

	return 0;
}

void Kis_ServerList_Picker::ConfigurePicker(string in_title, kpi_sl_cb_hook in_hook,
											void *in_aux) {
	SetTitle(in_title);
	cb_hook = in_hook;
	cb_aux = in_aux;
}

// Addcard callback is used to actually build the addcard window once
// we've picked a source.  This will be called directly from the main
// menu handlers if there aren't any sources.
void sp_addcard_cb(KPI_SL_CB_PARMS) {
	Kis_AddCard_Panel *acp = new Kis_AddCard_Panel(globalreg, kpi);

	acp->Position((LINES / 2) - 5, (COLS / 2) - (40 / 2), 10, 40);

	acp->SetTargetClient(picked);

	kpi->AddPanel(acp);
}

Kis_AddCard_Panel::Kis_AddCard_Panel(GlobalRegistry *in_globalreg, 
									 KisPanelInterface *in_intf) :
	Kis_Panel(in_globalreg, in_intf) {

	srctype = new Kis_Single_Input(globalreg, this);
	srciface = new Kis_Single_Input(globalreg, this);
	srcname = new Kis_Single_Input(globalreg, this);

	okbutton = new Kis_Button(globalreg, this);
	cancelbutton = new Kis_Button(globalreg, this);

	comp_vec.push_back(srctype);
	comp_vec.push_back(srciface);
	comp_vec.push_back(srcname);
	comp_vec.push_back(okbutton);
	comp_vec.push_back(cancelbutton);

	tab_components.push_back(srctype);
	tab_components.push_back(srciface);
	tab_components.push_back(srcname);
	tab_components.push_back(okbutton);
	tab_components.push_back(cancelbutton);
	tab_pos = 0;

	active_component = srctype;

	SetTitle("Add Source");

	srctype->SetLabel("Type", LABEL_POS_LEFT);
	srctype->SetTextLen(32);
	srctype->SetCharFilter(FILTER_ALPHANUMSYM);

	srciface->SetLabel("Intf", LABEL_POS_LEFT);
	srciface->SetTextLen(32);
	srciface->SetCharFilter(FILTER_ALPHANUMSYM);
		
	srcname->SetLabel("Name", LABEL_POS_LEFT);
	srcname->SetTextLen(32);
	srcname->SetCharFilter(FILTER_ALPHANUMSYM);

	okbutton->SetText("Add");
	cancelbutton->SetText("Cancel");

	target_cli = NULL;
}

Kis_AddCard_Panel::~Kis_AddCard_Panel() {
}

void Kis_AddCard_Panel::Position(int in_sy, int in_sx, int in_y, int in_x) {
	Kis_Panel::Position(in_sy, in_sx, in_y, in_x);

	srctype->SetPosition(2, 2, in_x - 6, 1);
	srciface->SetPosition(2, 4, in_x - 15, 1);
	srcname->SetPosition(2, 6, in_x - 6, 1);
	okbutton->SetPosition(in_x - 15, in_y - 2, 10, 1);
	cancelbutton->SetPosition(in_x - 15 - 2 - 15, in_y - 2, 10, 1);

	srctype->Activate(1);
	active_component = srctype;

	srctype->Show();
	srciface->Show();
	srcname->Show();
	
	okbutton->Show();
	cancelbutton->Show();
}

void Kis_AddCard_Panel::SetTargetClient(KisNetClient *in_cli) {
	target_cli = in_cli;

	ostringstream osstr;
	osstr << "Add Source to " << in_cli->FetchHost() << ":" << in_cli->FetchPort();

	SetTitle(osstr.str());
}

void Kis_AddCard_Panel::DrawPanel() {
	ColorFromPref(text_color, "panel_text_color");
	ColorFromPref(border_color, "panel_border_color");

	wbkgdset(win, text_color);
	werase(win);

	DrawTitleBorder();

	wattrset(win, text_color);

	DrawTitleBorder();

	for (unsigned int x = 0; x < comp_vec.size(); x++)
		comp_vec[x]->DrawComponent();

	wmove(win, 0, 0);
}

int Kis_AddCard_Panel::KeyPress(int in_key) {
	int ret;

	// Rotate through the tabbed items
	if (in_key == '\t') {
		tab_components[tab_pos]->Deactivate();
		tab_pos++;
		if (tab_pos >= (int) tab_components.size())
			tab_pos = 0;
		tab_components[tab_pos]->Activate(1);
		active_component = tab_components[tab_pos];
	}

	// Otherwise the menu didn't touch the key, so pass it to the top
	// component
	if (active_component != NULL) {
		ret = active_component->KeyPress(in_key);

		if (active_component == okbutton && ret == 1) {
			if (srctype->GetText() == "") {
				kpinterface->RaiseAlert("No source type",
										"No source type was provided for\n"
										"creating a new source.  A source\n"
										"type is required.\n");
				return(0);
			}

			if (srciface->GetText() == "") {
				kpinterface->RaiseAlert("No source interface",
										"No source interface was provided for\n"
										"creating a new source.  A source\n"
										"interface is required.\n");
				return(0);
			}

			if (srcname->GetText() == "") {
				kpinterface->RaiseAlert("No source name",
										"No source name was provided for\n"
										"reating a new source.  A source name\n"
										"is required.\n");
				return(0);
			}

			if (target_cli == NULL) {
				globalreg->panel_interface->KillPanel(this);
				return(0);
			}

			if (target_cli->Valid() == 0) {
				kpinterface->RaiseAlert("Server unavailable",
										"The selected server is not available.\n");
				return(0);
			}

			// Build a command and inject it
			string srccmd;
			srccmd = "ADDSOURCE " + srctype->GetText() + "," +
				srciface->GetText() + "," + srcname->GetText();

			target_cli->InjectCommand(srccmd);

			globalreg->panel_interface->KillPanel(this);
		} else if (active_component == cancelbutton && ret == 1) {
			// Cancel and close
			globalreg->panel_interface->KillPanel(this);
		}
	}

	return 0;
}

Kis_Plugin_Picker::Kis_Plugin_Picker(GlobalRegistry *in_globalreg, 
									 KisPanelInterface *in_intf) :
	Kis_Panel(in_globalreg, in_intf) {

	pluglist = new Kis_Scrollable_Table(globalreg, this);

	comp_vec.push_back(pluglist);

	vector<Kis_Scrollable_Table::title_data> titles;
	Kis_Scrollable_Table::title_data t;
	t.width = 55;
	t.title = "Plugin";
	t.alignment = 0;
	titles.push_back(t);

	pluglist->AddTitles(titles);

	// Grab the list of plugins we have loaded already, then combine it with the
	// plugins we scan from the directories.  This is anything but fast and
	// efficient, but we're not doing it very often -- not even every window
	// draw -- so whatever.
	vector<panel_plugin_meta *> *runningplugins = kpinterface->FetchPluginVec();
	vector<string> plugdirs = kpinterface->prefs.FetchOptVec("PLUGINDIR");

	for (unsigned int x = 0; x < runningplugins->size(); x++) {
		panel_plugin_meta pm;
		pm.filename = (*runningplugins)[x]->filename;
		pm.objectname = (*runningplugins)[x]->objectname;
		pm.dlfileptr = (void *) 0x1;
		listedplugins.push_back(pm);
	}

	for (unsigned int x = 0; x < plugdirs.size(); x++) {
		DIR *plugdir;
		struct dirent *plugfile;
		string expanddir = ConfigFile::ExpandLogPath(plugdirs[x], "", "", 0, 1);

		if ((plugdir = opendir(expanddir.c_str())) == NULL) {
			continue;
		}

		while ((plugfile = readdir(plugdir)) != NULL) {
			int loaded = 0;

			if (plugfile->d_name[0] == '.')
				continue;

			string fname = plugfile->d_name;

			if (fname.find(".so") == fname.length() - 3) {
				for (unsigned int y = 0; y < listedplugins.size(); y++) {
					if (listedplugins[y].filename == expanddir + fname) {
						loaded = 1;
						break;
					}
				}

				if (loaded)
					continue;

				panel_plugin_meta pm;
				pm.filename = expanddir + fname;
				pm.objectname = fname;
				pm.dlfileptr = (void *) 0x0;
				listedplugins.push_back(pm);
			}
		}

		closedir(plugdir);
	}

	for (unsigned int x = 0; x < listedplugins.size(); x++) {
		vector<string> td;
		string en = "";

		if (listedplugins[x].dlfileptr != (void *) 0x0)
			en = " (Loaded)";

		td.push_back(listedplugins[x].objectname + en);

		pluglist->ReplaceRow(x, td);
	}

	if (listedplugins.size() > 0) {
		vector<string> td;
		td.push_back("Cancel");
		pluglist->ReplaceRow(listedplugins.size(), td);
	}

	if (listedplugins.size() == 0) {
		vector<string> td;
		td.push_back(" ");
		td.push_back("No plugins found");
		pluglist->ReplaceRow(0, td);
	}

	active_component = pluglist;

	pluglist->Activate(1);

	SetTitle("");
}

Kis_Plugin_Picker::~Kis_Plugin_Picker() {
}

void Kis_Plugin_Picker::Position(int in_sy, int in_sx, int in_y, int in_x) {
	Kis_Panel::Position(in_sy, in_sx, in_y, in_x);

	pluglist->SetPosition(2, 1, in_x - 4, in_y - 2);

	pluglist->Show();
}

void Kis_Plugin_Picker::DrawPanel() {
	ColorFromPref(text_color, "panel_text_color");
	ColorFromPref(border_color, "panel_border_color");

	wbkgdset(win, text_color);
	werase(win);

	DrawTitleBorder();

	wattrset(win, text_color);

	for (unsigned int x = 0; x < comp_vec.size(); x++)
		comp_vec[x]->DrawComponent();

	wmove(win, 0, 0);
}

int Kis_Plugin_Picker::KeyPress(int in_key) {
	int ret;
	int listkey;
	
	// Rotate through the tabbed items
	if (in_key == '\n' || in_key == '\r') {
		listkey = pluglist->GetSelected();

		if (listkey >= 0 && listkey <= (int) listedplugins.size()) {
			if (listkey < (int) listedplugins.size()) {
				if (listedplugins[listkey].dlfileptr == 0x0) {
					kpinterface->LoadPlugin(listedplugins[listkey].filename,
											listedplugins[listkey].objectname);
				}
			}
		}

		globalreg->panel_interface->KillPanel(this);
	}

	// Otherwise the menu didn't touch the key, so pass it to the top
	// component
	if (active_component != NULL) {
		ret = active_component->KeyPress(in_key);
	}

	return 0;
}

#endif

