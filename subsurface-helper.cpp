// SPDX-License-Identifier: GPL-2.0
#include <QQmlEngine>
#include <QDebug>
#include <QQuickItem>

#include "map-widget/qmlmapwidgethelper.h"
#include "qt-models/maplocationmodel.h"
#include "core/qt-gui.h"
#include "core/settings/qPref.h"
#include "core/ssrf.h"

#ifdef SUBSURFACE_MOBILE
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "mobile-widgets/themeinterface.h"
#include "mobile-widgets/qmlmanager.h"
#include "mobile-widgets/qmlinterface.h"
#include "qt-models/divelistmodel.h"
#include "qt-models/divesummarymodel.h"
#include "qt-models/mobilelistmodel.h"
#include "qt-models/mobilefiltermodel.h"
#include "qt-models/gpslistmodel.h"
#include "qt-models/messagehandlermodel.h"
#include "profile-widget/qmlprofile.h"
#include "core/downloadfromdcthread.h"
#include "qt-models/diveimportedmodel.h"
#include "mobile-widgets/qml/kirigami/src/kirigamiplugin.h"
#else
#include "desktop-widgets/mainwindow.h"
#endif

#if defined(Q_OS_ANDROID)
QString getAndroidHWInfo(); // from android.cpp
#include <QApplication>
#include <QFontDatabase>
#endif /* Q_OS_ANDROID */

#ifndef SUBSURFACE_TEST_DATA
QObject *qqWindowObject = NULL;

// Forward declaration
static void register_qml_types(QQmlEngine *);
static void register_meta_types();

void init_ui()
{
	init_qt_late();
	register_meta_types();
#ifndef SUBSURFACE_MOBILE
	register_qml_types(NULL);

	MainWindow *window = new MainWindow();
	window->setTitle();
#endif // SUBSURFACE_MOBILE
}

void exit_ui()
{
#ifndef SUBSURFACE_MOBILE
	delete MainWindow::instance();
#endif // SUBSURFACE_MOBILE
	free((void *)existing_filename);
}

void run_ui()
{
#ifdef SUBSURFACE_MOBILE
#if defined(Q_OS_ANDROID)
	if (getAndroidHWInfo().contains("/OnePlus/")) {
		QFontDatabase db;
		int id = QFontDatabase::addApplicationFont(":/fonts/Roboto-Regular.ttf");
		QString family = QFontDatabase::applicationFontFamilies(id).at(0);
		QFont newDefaultFont;
		newDefaultFont.setFamily(family);
		(static_cast<QApplication *>(QCoreApplication::instance()))->setFont(newDefaultFont);
		qDebug() << "Detected OnePlus device, trying to force bundled font" << family;
		QFont defaultFont = (static_cast<QApplication *>(QCoreApplication::instance()))->font();
		qDebug() << "Qt reports default font is set as" << defaultFont.family();
	}
#endif
	QScreen *appScreen = QApplication::screens().at(0);
	int availableScreenWidth = appScreen->availableSize().width();
	QQmlApplicationEngine engine;
	register_qml_types(&engine);
	KirigamiPlugin::getInstance().registerTypes();
#if defined(__APPLE__) && !defined(Q_OS_IOS)
	// when running the QML UI on a Mac the deployment of the QML Components seems
	// to fail and the search path for the components is rather odd - simply the
	// same directory the executable was started from <bundle>/Contents/MacOS/
	// To work around this we need to manually copy the components at install time
	// to Contents/Frameworks/qml and make sure that we add the correct import path
	const QStringList importPathList = engine.importPathList();
	for (QString importPath: importPathList) {
		if (importPath.contains("MacOS"))
			engine.addImportPath(importPath.replace("MacOS", "Frameworks"));
	}
	qDebug() << "QML import path" << engine.importPathList();
#endif // __APPLE__ not Q_OS_IOS
	engine.addImportPath("qrc://imports");
	QSortFilterProxyModel *gpsSortModel = new QSortFilterProxyModel(nullptr);
	gpsSortModel->setSourceModel(GpsListModel::instance());
	gpsSortModel->setDynamicSortFilter(true);
	gpsSortModel->setSortRole(GpsListModel::GpsWhenRole);
	gpsSortModel->sort(0, Qt::DescendingOrder);
	QQmlContext *ctxt = engine.rootContext();
	MobileFilterModel *mfm = MobileFilterModel::instance();
	ctxt->setContextProperty("diveModel", mfm);
	ctxt->setContextProperty("gpsModel", gpsSortModel);
	ctxt->setContextProperty("vendorList", vendorList);
	set_non_bt_addresses();

	ctxt->setContextProperty("connectionListModel", &connectionListModel);
	ctxt->setContextProperty("logModel", MessageHandlerModel::self());

	qmlRegisterUncreatableType<QMLManager>("org.subsurfacedivelog.mobile",1,0,"ExportType","Enum is not a type");

#ifdef SUBSURFACE_MOBILE_DESKTOP
	if (testqml) {
		QString fileLoad(testqml);
		fileLoad += "/main.qml";
		engine.load(QUrl(fileLoad));
	} else {
		engine.load(QUrl(QStringLiteral("qrc:///qml/main.qml")));
	}
#else
	engine.load(QUrl(QStringLiteral("qrc:///qml/main.qml")));
#endif
	qDebug() << "loaded main.qml";
	qqWindowObject = engine.rootObjects().value(0);
	if (!qqWindowObject) {
		fprintf(stderr, "can't create window object\n");
		exit(1);
	}
	QQuickWindow *qml_window = qobject_cast<QQuickWindow *>(qqWindowObject);
	qml_window->setIcon(QIcon(":subsurface-mobile-icon"));
	qDebug() << "qqwindow devicePixelRatio" << qml_window->devicePixelRatio() << qml_window->screen()->devicePixelRatio();
	QScreen *screen = qml_window->screen();
	int qmlWW = qml_window->width();
	int qmlSW = screen->size().width();
	qDebug() << "qml_window reports width as" << qmlWW << "associated screen width" << qmlSW << "Qt screen reports width as" << availableScreenWidth;
	QObject::connect(qml_window, &QQuickWindow::screenChanged, QMLManager::instance(), &QMLManager::screenChanged);
	QMLManager *manager = QMLManager::instance();

	manager->setDevicePixelRatio(qml_window->devicePixelRatio(), qml_window->screen());
	manager->qmlWindow = qqWindowObject;
	manager->screenChanged(screen);
	qDebug() << "qqwindow screen has ldpi/pdpi" << screen->logicalDotsPerInch() << screen->physicalDotsPerInch();
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
	int width = 800;
	int height = 1200;
	if (qEnvironmentVariableIsSet("SUBSURFACE_MOBILE_WIDTH")) {
		bool ok;
		int width_override = qEnvironmentVariableIntValue("SUBSURFACE_MOBILE_WIDTH", &ok);
		if (ok) {
			width = width_override;
			qDebug() << "overriding window width:" << width;
		}
	}
	if (qEnvironmentVariableIsSet("SUBSURFACE_MOBILE_HEIGHT")) {
		bool ok;
		int height_override = qEnvironmentVariableIntValue("SUBSURFACE_MOBILE_HEIGHT", &ok);
		if (ok) {
			height = height_override;
			qDebug() << "overriding window height:" << height;
		}
	}
	qml_window->setHeight(height);
	qml_window->setWidth(width);
#endif // not Q_OS_ANDROID and not Q_OS_IOS
	qml_window->show();
#else
	MainWindow::instance()->show();
#endif // SUBSURFACE_MOBILE
	qApp->exec();
}

Q_DECLARE_METATYPE(duration_t)
static void register_meta_types()
{
	qRegisterMetaType<duration_t>();
}
#endif // not SUBSURFACE_TEST_DATA

#define REGISTER_TYPE(useClass, useQML) \
	rc = qmlRegisterType<useClass>("org.subsurfacedivelog.mobile", 1, 0, useQML); \
	if (rc < 0) \
		qWarning() << "ERROR: Cannot register " << useQML << ", QML will not work!!";

void register_qml_types(QQmlEngine *engine)
{
	// register qPref*
	qPref::registerQML(engine);

#ifndef SUBSURFACE_TEST_DATA
	int rc;

#ifdef SUBSURFACE_MOBILE
	if (engine != NULL) {
		QQmlContext *ct = engine->rootContext();

		// Register qml interface classes
		QMLInterface::setup(ct);
		themeInterface::setup(ct);
	}

	REGISTER_TYPE(QMLManager, "QMLManager");
	REGISTER_TYPE(QMLProfile, "QMLProfile");
	REGISTER_TYPE(DiveImportedModel, "DCImportModel");
	REGISTER_TYPE(DiveSummaryModel, "DiveSummaryModel");
#endif // not SUBSURFACE_MOBILE

	REGISTER_TYPE(MapWidgetHelper, "MapWidgetHelper");
	REGISTER_TYPE(MapLocationModel, "MapLocationModel");
#endif // not SUBSURFACE_TEST_DATA
}
