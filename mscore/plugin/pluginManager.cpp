//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2011 Werner Schweer and others
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include "pluginManager.h"
#include "qmlpluginengine.h"
#include "shortcutcapturedialog.h"
#include "musescore.h"
#include "libmscore/xml.h"
#include "preferences.h"
#include "qmlplugin.h"
#include <QQuickView>
#include <QGuiApplication>
#include <QTimer>
#include <QJSValue>
#include <QSignalBlocker>


namespace Ms {

static const QByteArray pluginShortcutActionName = "plugin-run";

//---------------------------------------------------------
//   PluginManager
//---------------------------------------------------------

PluginManager::PluginManager(QWidget* parent)
   : QDialog(parent)
      {
      setObjectName("PluginManager");
      setupUi(this);
      setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);
      connect(definePluginShortcut, SIGNAL(clicked()), SLOT(definePluginShortcutClicked()));
      connect(clearPluginShortcut, SIGNAL(clicked()), SLOT(clearPluginShortcutClicked()));
      connect(reloadPlugins, SIGNAL(clicked()), SLOT(reloadPluginsClicked()));
      readSettings();
      }

//---------------------------------------------------------
//   init
//---------------------------------------------------------

void PluginManager::init()
      {
      //
      // initialize local shortcut table
      //    we need a deep copy to be able to rewind all
      //    changes on "Abort"
      //
      qDeleteAll(localShortcuts);
      localShortcuts.clear();
      foreach(const Shortcut* s, Shortcut::shortcuts())
            localShortcuts[s->key()] = new Shortcut(*s);
      shortcutsChanged = false;
      loadList(false);
}

//---------------------------------------------------------
//   readPluginList
//---------------------------------------------------------

bool PluginManager::readPluginList()
      {
      QFile f(dataPath + "/plugins.xml");
      if (!f.exists())
            return false;
      if (!f.open(QIODevice::ReadOnly)) {
            qDebug("Cannot open plugins file <%s>", qPrintable(f.fileName()));
            return false;
            }
      XmlReader e(&f);
      while (e.readNextStartElement()) {
            if (e.name() == "museScore") {
                  while (e.readNextStartElement()) {
                        const QStringRef& tag(e.name());
                        if (tag == "Plugin") {
                              PluginDescription d;
                              while (e.readNextStartElement()) {
                                    const QStringRef& t(e.name());
                                    if (t == "path")
                                          d.path = e.readElementText();
                                    else if (t == "load")
                                          d.load = e.readInt();
                                    else if (t == "SC")
                                          d.shortcut.read(e);
                                    else if (t == "version")
                                          d.version = e.readElementText();
                                    else if (t == "description")
                                          d.description = e.readElementText();
                                    else
                                          e.unknown();
                                    }
                              d.shortcut.setState(STATE_NORMAL | STATE_NOTE_ENTRY | STATE_EDIT |
                                          STATE_ALLTEXTUAL_EDIT | STATE_PLAY | STATE_FOTO | STATE_LOCK );
                              d.shortcut.setKey(pluginShortcutActionName);
                              if (d.path.endsWith(".qml"))
                                    _pluginList.append(d);
                              }
                        else
                              e.unknown();
                        }
                  }
            else
                  e.unknown();
            }
      return true;
}

//---------------------------------------------------------
//   writePluginList
//---------------------------------------------------------

void PluginManager::writePluginList()
      {
      QDir dir;
      dir.mkpath(dataPath);
      QFile f(dataPath + "/plugins.xml");
      if (!f.open(QIODevice::WriteOnly)) {
            qDebug("cannot create plugin file <%s>", qPrintable(f.fileName()));
            return;
            }
      XmlWriter xml(0, &f);
      xml.header();
      xml.stag("museScore version=\"" MSC_VERSION "\"");
      foreach(const PluginDescription& d, _pluginList) {
            xml.stag("Plugin");
            xml.tag("path", d.path);
            xml.tag("load", d.load);
            xml.tag("version", d.version);
            xml.tag("description", d.description);
            if (!d.shortcut.keys().isEmpty())
                  d.shortcut.write(xml);
            xml.etag();
            }
      xml.etag();
      f.close();
}

//---------------------------------------------------------
//   updatePluginList
//    scan plugin folders for new plugins and update
//    pluginList
//---------------------------------------------------------

#ifdef SCRIPT_INTERFACE
static void updatePluginList(const QString& pluginPath,
   QList<PluginDescription>& pluginList, QmlPluginEngine* metadataEngine)
      {
      QDirIterator it(pluginPath, QDir::NoDot|QDir::NoDotDot|QDir::Dirs|QDir::Files,
         QDirIterator::Subdirectories);
      while (it.hasNext()) {
            it.next();
            QFileInfo fi = it.fileInfo();
            QString path(fi.absoluteFilePath());
            if (fi.isFile()) {
                  if (path.endsWith(".qml")) {
                        bool alreadyInList = false;
                        foreach (const PluginDescription& p, pluginList) {
                              if (p.path == path) {
                                    alreadyInList = true;
                                    break;
                                    }
                              }
                        if (!alreadyInList) {
                              PluginDescription p;
                              p.path = path;
                              p.load = false;
                              p.shortcut.setKey(pluginShortcutActionName);
                              if (collectPluginMetaInformation(&p,metadataEngine))
                                    pluginList.append(p);
                              }
                        }
                  }

            }
      }
#endif

void PluginManager::updatePluginList(bool forceRefresh)
      {
#ifdef SCRIPT_INTERFACE
      QList<QString> pluginPathList;
      pluginPathList.append(dataPath + "/plugins");
      pluginPathList.append(mscoreGlobalShare + "plugins");
      QString p = preferences.getString(PREF_APP_PATHS_MYPLUGINS);
      if (!p.isEmpty())
            pluginPathList.append(p);
      if (forceRefresh) {
            _pluginList.clear();
            // Every running panel has its own engine. Metadata scans use a short-lived engine.
            }

      QmlPluginEngine metadataEngine;
      for (const QString& _pluginPath : pluginPathList) {
            Ms::updatePluginList(_pluginPath, _pluginList, &metadataEngine);
            }
      //remove non existing files
      auto i = _pluginList.begin();
      while (i != _pluginList.end()) {
            PluginDescription d = *i;
            QFileInfo fi(d.path);
            if (!fi.exists())
                  i = _pluginList.erase(i);
            else
                  ++i;
            }
#endif
      }

//---------------------------------------------------------
//   loadList - populate the listbox.
//---------------------------------------------------------

void PluginManager::loadList(bool forceRefresh)
      {
      const QSignalBlocker block(pluginListWidget);
      const auto previous=_pluginList;
      QString selectedPath;
      if(auto item=pluginListWidget->currentItem()){int i=item->data(Qt::UserRole).toInt();if(i>=0&&i<_pluginList.size())selectedPath=_pluginList[i].path;}
      QStringList saveLoaded; // Keep enabled plugins and shortcuts during a rescan.
      int n = _pluginList.size();
      if (forceRefresh && n > 0) {
            for (int i = 0; i < n; i++) {
                  PluginDescription& d = _pluginList[i];
                  if (d.load) {
                        saveLoaded.append(d.path);
                        mscore->unregisterPlugin(&d);  // This will force the menu to rebuild.
                        }
                  }
            }
      updatePluginList(forceRefresh);
      n = _pluginList.size();
      pluginListWidget->clear();
      disconnect(pluginListWidget, nullptr, nullptr, nullptr);
      for (int i = 0; i < n; ++i) {
            PluginDescription& d = _pluginList[i];
            if(forceRefresh)for(const auto& old:previous)if(old.path==d.path){d.shortcut=old.shortcut;break;}
            Shortcut* s = &d.shortcut;
            if(!localShortcuts.contains(s->key()))localShortcuts[s->key()] = new Shortcut(*s);
            if (saveLoaded.contains(d.path)) d.load = true;
            QListWidgetItem* item = new QListWidgetItem(QFileInfo(d.path).completeBaseName(),  pluginListWidget);
            item->setFlags(item->flags() | Qt::ItemIsEnabled);
            item->setCheckState(d.load ? Qt::Checked : Qt::Unchecked);
            item->setData(Qt::UserRole, i);
            }
      connect(pluginListWidget, SIGNAL(itemChanged(QListWidgetItem*)), SLOT(pluginLoadToggled(QListWidgetItem*)));
      connect(pluginListWidget, SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)),
              SLOT(pluginListWidgetItemChanged(QListWidgetItem*,QListWidgetItem*)));
      if (n) {
            int selected=0;for(int i=0;i<n;++i)if(_pluginList[i].path==selectedPath)selected=i;
            pluginListWidget->setCurrentRow(selected);
            pluginListWidgetItemChanged(pluginListWidget->item(selected), 0);
            }
      }

//---------------------------------------------------------
//   apply
//---------------------------------------------------------

void PluginManager::accept()
      {
      if (shortcutsChanged) {
            shortcutsChanged = false;
            foreach(const Shortcut* s, localShortcuts) {
                  Shortcut* os = Shortcut::getShortcut(s->key());
                  if (os) {
                        if (!os->compareKeys(*s))
                              os->setKeys(s->keys());
                        }
                  }
            Shortcut::dirty = true;
            }
      int n = _pluginList.size();
      for (int i = 0; i < n; ++i) {
            PluginDescription& d = _pluginList[i];
            if (d.load)
                  mscore->registerPlugin(&d);
            else
                  mscore->unregisterPlugin(&d);
            }

      writePluginList();
      if (Shortcut::dirty)
            Shortcut::save();
      Shortcut::dirty = false;

      QDialog::accept();
      }

//---------------------------------------------------------
//   closeEvent
//---------------------------------------------------------

void PluginManager::closeEvent(QCloseEvent* ev)
      {
      emit closed(false);
      QWidget::closeEvent(ev);
      }

//---------------------------------------------------------
//   pluginListWidgetItemChanged
//---------------------------------------------------------

void PluginManager::pluginListWidgetItemChanged(QListWidgetItem* item, QListWidgetItem*)
      {
      if (!item)
            return;
      int idx = item->data(Qt::UserRole).toInt();
      const PluginDescription& d = _pluginList[idx];
      QFileInfo fi(d.path);
      pluginName->setText(fi.completeBaseName());
      pluginPath->setText(fi.absolutePath());
      pluginVersion->setText(d.version);
      pluginShortcut->setText(d.shortcut.keysToString());
      pluginDescription->setText(d.description);
      }

//---------------------------------------------------------
//   pluginLoadToggled
//---------------------------------------------------------

void PluginManager::pluginLoadToggled(QListWidgetItem* item)
      {
      int idx = item->data(Qt::UserRole).toInt();
      PluginDescription* d = &_pluginList[idx];
      d->load = (item->checkState() == Qt::Checked);
      }

//---------------------------------------------------------
//   definePluginShortcutClicked
//---------------------------------------------------------

void PluginManager::definePluginShortcutClicked()
      {
      QListWidgetItem* item = pluginListWidget->currentItem();
      if (!item)
            return;
      int idx = item->data(Qt::UserRole).toInt();
      PluginDescription* pd = &_pluginList[idx];
      Shortcut* s = &pd->shortcut;
      ShortcutCaptureDialog sc(s, localShortcuts, this);
      int rv = sc.exec();
      if (rv == 0)            // abort
            return;
      if (rv == 2)            // replace
            s->clear();

      s->addShortcut(sc.getKey());
      QAction* action = s->action();
      action->setShortcuts(s->keys());
      mscore->addAction(action);

      pluginShortcut->setText(s->keysToString());
      }

//---------------------------------------------------------
//   reloadPluginShortcutClicked
//---------------------------------------------------------

void PluginManager::reloadPluginsClicked()
      {
      QList<QPointer<QmlPlugin>> panels;
      bool waiting=QApplication::mouseButtons()!=Qt::NoButton;
      for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))
            if(auto panel=qobject_cast<QmlPlugin*>(view->rootObject())){
                  auto capture=panel->property("captureState").value<QJSValue>().toVariant().toMap();
                  waiting=waiting||capture.value("recording").toBool()||capture.value("stopping").toBool()||capture.value("saving").toBool();
                  if(!panel->property("workflowPluginPath").toString().isEmpty())panels.append(panel);
                  }
      if(waiting){
            reloadPlugins->setEnabled(false);reloadPlugins->setToolTip(tr("录制或拖动结束后将重载插件。"));
            if(!_reloadWaiting){_reloadWaiting=true;QTimer::singleShot(200,this,[this]{_reloadWaiting=false;reloadPluginsClicked();});}return;
            }
      reloadPlugins->setEnabled(false);
      QStringList reopen;
      for(auto panel:panels)if(panel){reopen.append(panel->property("workflowPluginPath").toString());panel->disposePanelForReload();}
      QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
      loadList(true);
      for(auto& plugin:_pluginList)if(plugin.load)mscore->registerPlugin(&plugin);
      for(const auto& path:reopen)mscore->pluginTriggered(path);
      reloadPlugins->setEnabled(true);reloadPlugins->setToolTip(tr("重新读取插件，并恢复打开的面板。"));
      QMessageBox::information(this,tr("MuseScore"),tr("插件已重载，打开的面板已恢复。"));
      }

//---------------------------------------------------------
//   clearPluginShortcutClicked
//---------------------------------------------------------

void PluginManager::clearPluginShortcutClicked()
      {
      QListWidgetItem* item = pluginListWidget->currentItem();
      if (!item)
            return;
      int idx = item->data(Qt::UserRole).toInt();
      PluginDescription* pd = &_pluginList[idx];
      Shortcut* s = &pd->shortcut;
      s->clear();

      QAction* action = s->action();
      action->setShortcuts(s->keys());
//      mscore->addAction(action);

      pluginShortcut->setText(s->keysToString());
      }

//---------------------------------------------------------
//   writeSettings
//---------------------------------------------------------

void PluginManager::writeSettings()
      {
      MuseScore::saveGeometry(this);
      }

//---------------------------------------------------------
//   readSettings
//---------------------------------------------------------

void PluginManager::readSettings()
      {
      MuseScore::restoreGeometry(this);
      }

}
