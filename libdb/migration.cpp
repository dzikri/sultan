/*
 * migration.cpp
 * Copyright 2017 - ~, Apin <apin.klas@gmail.com>
 *
 * This file is part of Sultan.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "migration.h"
#include "db.h"
#include <QDebug>

using namespace LibDB;

static QString TAG{"MIGRATION"};
static std::function<bool(const QString &)> sAfterMigrate = nullptr;

bool Migration::migrateAll(const QString &folder, const QString &dbtype, std::function<bool(const QString &)> afterCallback)
{
    auto db = Db::createInstance(false, true);
    Migration mg(db, folder, dbtype);
    mg.mAfterMigrate = afterCallback;
    return mg.migrate();
}

void Migration::setAfterMigrate(std::function<bool (const QString &)> afterCallback)
{
    sAfterMigrate = afterCallback;
}

Migration::Migration(Db *db, const QString &folder, const QString &dbtype):
    mDb(db),
    mDir(QDir(folder)),
    mDbType(dbtype)
{
    init();
}

bool Migration::migrate()
{
    auto files = mDir.entryList(QDir::NoFilter, QDir::Name);
    if(files.isEmpty()) {
        qDebug() << TAG << "No file on folder";
        return false;
    }
    bool started = false;
    bool errorOccure = false;
    const QString oldLastFile = mLastFile;
    for(const QString &file : files) {
        if(!file.compare(".") || !file.compare("..")) continue;
        if(!started) {
            if(mLastFile.isEmpty()) {
                started = true;
            } else if(mLastFile.compare(file) == 0) {
                started = true;
                continue;
            }
        }
        if(started) {
            if(executeFile(file)) {
                mLastFile = file;
                if(sAfterMigrate != nullptr) {
                    if(!sAfterMigrate(mLastFile)) {
                        errorOccure = true;
                        break;
                    }
                }
                if(mAfterMigrate != nullptr) {
                    if(!mAfterMigrate(mLastFile)) {
                        errorOccure = true;
                        break;
                    }
                }
            } else {
                errorOccure = true;
                break;
            }
        }
    }
    if(mLastFile.compare(oldLastFile)) {
        QVariantMap data;
        data.insert("name", mLastFile);
        mDb->where("name = ", oldLastFile)->update("migrations", data);
    }
    return !errorOccure;
}

void Migration::init()
{
    if(!mDb->exec("SELECT 1 FROM migrations LIMIT 1")) {
        mDb->exec("CREATE TABLE migrations (name VARCHAR(255) NOT NULL DEFAULT '');");
        QVariantMap data;
        data.insert("name", "");
        mDb->insert("migrations", data);
    } else {
        DbResult res = mDb->table("migrations")->limit(1)->exec();
        if(!res.isEmpty())
            mLastFile = res.first().value("name").toString();
    }
}

bool Migration::executeFile(const QString &filename)
{
    QFile file(mDir.absoluteFilePath(filename));
    if(!file.open(QFile::ReadOnly)) {
        qCritical() << TAG << "Can not open file" << file.fileName();
        return false;
    }
    const QString sqlCommand(file.readAll());
    if(mDbType == "SQLITE") {
        const QStringList sqllist = sqlCommand.split("-- separator");
        for(const QString &cmd : sqllist) {
            bool ret = mDb->exec(cmd);
            if(!ret) {
                qCritical() << TAG << "Migration file" << filename << "error :" << mDb->lastError().text();
                return false;
            }
        }
        return true;
    } else {
        bool ret = mDb->exec(sqlCommand);
        if(!ret)
            qCritical() << TAG << "Migration file" << filename << "error :" << mDb->lastError().text();
        return ret;
    }
}
