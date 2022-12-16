.NOTINTERMEDIATE:

test1:
	touch foo.x

test2: foo.z

%.z: %.y
	cp $< $@

%.y: %.x
	cp $< $@
