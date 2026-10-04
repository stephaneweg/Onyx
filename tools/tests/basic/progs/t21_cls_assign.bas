' compile error: two classes without a link
CLASS A
END CLASS
CLASS B
END CLASS
DIM a AS A, b AS B
a = NEW A
b = a
