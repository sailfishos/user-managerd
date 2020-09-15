TEMPLATE = aux

CONFIG += mer-qdoc-template
MER_QDOC.project = user-managerd
MER_QDOC.config = user-managerd.qdocconf
MER_QDOC.style = offline
MER_QDOC.path = /usr/share/doc/user-managerd/

OTHER_FILES += \
    user-managerd.qdocconf \
    index.qdoc \
    sailfishusermanagerinterface.qdoc
