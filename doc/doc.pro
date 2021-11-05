TEMPLATE = aux

CONFIG += sailfish-qdoc-template
SAILFISH_QDOC.project = user-managerd
SAILFISH_QDOC.config = user-managerd.qdocconf
SAILFISH_QDOC.style = offline
SAILFISH_QDOC.path = /usr/share/doc/user-managerd/

OTHER_FILES += \
    user-managerd.qdocconf \
    index.qdoc \
    sailfishusermanagerinterface.qdoc
