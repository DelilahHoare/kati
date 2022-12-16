.NOTINTERMEDIATE:

test1:
	touch foo.x.x

test2: foo

%: %.x
	cp $< $@
