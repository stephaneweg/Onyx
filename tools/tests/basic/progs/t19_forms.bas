' t19_forms.bas -- what QBStudio's code needs: PROPERTY (get / set) in a CLASS, the controls moved / shown / enabled /
' focused, a resizable window and its -2 (resized) event, WINDOWWIDTH / WINDOWHEIGHT, menu items (their ids)
CLASS Control
  id AS INTEGER
  Text AS FUNCTION
END CLASS
PROPERTY Control.Text AS STRING
  RETURN "text of" + STR$(this.id)
END PROPERTY
PROPERTY Control.Text (v AS STRING)
  SETTEXT this.id, v
END PROPERTY
PROPERTY Control.Enabled (v AS INTEGER)
  ENABLECONTROL this.id, v
END PROPERTY
PROPERTY Control.Enabled AS INTEGER
  RETURN -1
END PROPERTY
SUB Control.Move (x, y, w, h)
  MOVECONTROL this.id, x, y, w, h
END SUB

WINDOW "Forms", 380, 260, 1
DIM SHARED b AS Control (), t AS Control ()
b.id = BUTTON(10, 10, 80, 28, "Go")
t.id = TEXTBOX(10, 50, 200, 26, "")
mQuit = MENUITEM("&File", "&Quit", "Ctrl+Q")
mSep = MENUITEM("&File", "-")
t.Text = "hello"
PRINT t.Text
b.Enabled = 0
PRINT "enabled"; b.Enabled
b.Move 20, 30, 100, 32
SHOWCONTROL t.id, 0
FOCUSCONTROL b.id
PRINT "size"; WINDOWWIDTH; WINDOWHEIGHT
DO
  e = WAITEVENT
  SELECT CASE e
  CASE -1: PRINT "closed": EXIT DO
  CASE -2: PRINT "resized"
  CASE b.id: PRINT "click"
  CASE mQuit: PRINT "quit": EXIT DO
  CASE ELSE: PRINT "event"; e
  END SELECT
LOOP
