/*
 * Copyright (c) 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * All rights reserved.
 *
 * BSD 3-Clause License, see LICENSE.
 */

#include <QCoreApplication>

#include "sailfishusermanager.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    SailfishUserManager daemon;
    return app.exec();
}
