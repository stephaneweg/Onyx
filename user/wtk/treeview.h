//
// wtk/treeview.h -- TreeView: a hierarchy of labelled nodes with expand / collapse
// (WPF TreeView). add (parent, label) returns the node id (parent -1 = a root node).
// Click the [+]/[-] box (or double-click, or Right / Left) to expand / collapse; click
// selects (onSelect), double-click on a leaf or Enter activates (onActivate); Up / Down
// move; the wheel and the scrollbar scroll. Read `sel` (node id, -1 = none).
//
#ifndef _wtk_treeview_h
#define _wtk_treeview_h

#include "wtk/widget.h"

namespace wtk {

class TreeView : public Widget
{
public:
	int sel, top; Action onSelect, onActivate;
	TreeView (int l, int t, int w, int h, Action onSelect_ = 0, Action onActivate_ = 0);
	~TreeView ();
	int  add (int parent, const char *label);
	void clear ();
	void expand (int id, bool open);
	bool expanded (int id) const { return id >= 0 && id < m_n && m_nodes[id].open; }
	bool hasChildren (int id) const;
	const char *label (int id) const { return (id >= 0 && id < m_n) ? m_nodes[id].label : ""; }
	int  parentOf (int id) const { return (id >= 0 && id < m_n) ? m_nodes[id].parent : -1; }
	int  userData (int id) const { return (id >= 0 && id < m_n) ? m_nodes[id].data : 0; }
	void setUserData (int id, int d) { if (id >= 0 && id < m_n) m_nodes[id].data = d; }
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
private:
	struct Node { char label[48]; int parent, depth, data; bool open; };
	Node *m_nodes; int m_n, m_cap;
	int *m_vis; int m_nvis;			// visible node ids, in display order
	unsigned m_lastClick; int m_lastRow; bool m_thumb;
	void rebuild ();
	void walk (int parent);
	int  rows () const;
	int  rowOf (int id) const;
	void scrollTo (int t);
	void pick (int id, bool fire);
};

} // namespace wtk

#endif
