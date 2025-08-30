// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/dialogs/settingsdialog/network.h"
#include "desktop/dialogs/avatarimport.h"
#include "desktop/dialogs/settingsdialog/helpers.h"
#include "desktop/main.h"
#include "desktop/settings.h"
#include "desktop/utils/accountlistmodel.h"
#include "desktop/utils/widgetutils.h"
#include "desktop/widgets/kis_slider_spin_box.h"
#include "libclient/utils/avatarlistmodel.h"
#include "libclient/utils/avatarlistmodeldelegate.h"
#include "libshared/net/proxy.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QModelIndex>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#ifdef Q_OS_ANDROID
#	include "libshared/util/androidutils.h"
#endif

namespace dialogs {
namespace settingsdialog {

Network::Network(desktop::settings::Settings &settings, QWidget *parent)
	: Page(parent)
{
	init(settings);
}

void Network::setUp(desktop::settings::Settings &settings, QVBoxLayout *layout)
{
	initAvatars(layout);
	utils::addFormSeparator(layout);
	initNetwork(settings, utils::addFormSection(layout));
#ifdef DP_HAVE_BUILTIN_SERVER
	utils::addFormSeparator(layout);
	initBuiltinServer(settings, utils::addFormSection(layout));
#endif
}

void Network::initAvatars(QVBoxLayout *layout)
{
	auto *avatarsLabel = new QLabel(tr("Chat avatars:"), this);
	layout->addWidget(avatarsLabel);

	auto *avatars = new QListView(this);
	avatarsLabel->setBuddy(avatars);
	avatars->setViewMode(QListView::IconMode);
	avatars->setResizeMode(QListView::Adjust);
	avatars->setMovement(QListView::Static);
	avatars->setUniformItemSizes(true);
	avatars->setWrapping(true);
	avatars->setMinimumHeight(40);
	avatars->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
	avatars->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
	avatars->setSelectionMode(QAbstractItemView::ExtendedSelection);
	utils::bindKineticScrollingWith(
		avatars, Qt::ScrollBarAlwaysOff, Qt::ScrollBarAsNeeded);

	auto *avatarsModel = new AvatarListModel(true, this);
	avatarsModel->loadAvatars();
	avatars->setModel(avatarsModel);
	avatars->setItemDelegate(new AvatarItemDelegate(this));

	layout->addWidget(avatars);
	layout->addLayout(listActions(
		avatars, tr("Add"), tr("Add avatar…"),
		[=] {
			AvatarImport::importAvatar(avatarsModel, this);
		},
		tr("Remove"), tr("Delete selected avatars…"),
		makeDefaultDeleter(
			this, avatars, tr("Delete avatars"),
			QT_TR_N_NOOP("Really delete %n avatar(s)?"))));
}

void Network::initBuiltinServer(
	desktop::settings::Settings &settings, QFormLayout *form)
{
	auto *port = new QSpinBox;
	port->setAlignment(Qt::AlignLeft);
	port->setRange(1, UINT16_MAX);
	settings.bindServerPort(port);
	form->addRow(
		tr("Builtin server:"),
		utils::encapsulate(tr("Host on port %1 if available"), port));
}

void Network::initNetwork(
	desktop::settings::Settings &settings, QFormLayout *form)
{
	auto *checkForUpdates =
		new QCheckBox(tr("Automatically check for updates"), this);
	settings.bindUpdateCheckEnabled(checkForUpdates);
	form->addRow(tr("Updates:"), checkForUpdates);

	auto *allowInsecure =
		new QCheckBox(tr("Allow insecure local storage"), this);
	settings.bindInsecurePasswordStorage(allowInsecure);
	connect(allowInsecure, &QCheckBox::clicked, this, [](bool checked) {
		if(!checked) {
			AccountListModel(dpApp().state(), nullptr).clearFallbackPasswords();
		}
	});
	form->addRow(tr("Password security:"), allowInsecure);

	auto *allowInsecureNotice = utils::formNote(
		tr("With this enabled, Drawpile may save passwords in an unencrypted "
		   "format. Disabling it will forget any insecurely stored passwords."),
		QSizePolicy::Label, QIcon::fromTheme("dialog-warning"));
	form->addRow(nullptr, allowInsecureNotice);
	settings.bindInsecurePasswordStorage(
		allowInsecureNotice, &QWidget::setVisible);

	auto *autoReset = utils::addRadioGroup(
		form, tr("Connection quality:"), true,
		{{tr("Good"), 1}, {tr("Poor"), 0}});
	settings.bindServerAutoReset(autoReset);

	auto *timeout = new QSpinBox(this);
	timeout->setAlignment(Qt::AlignLeft);
	timeout->setRange(15, 600);
	settings.bindServerTimeout(timeout);
	form->addRow(
		tr("Network timeout:"), utils::encapsulate(tr("%1 seconds"), timeout));

#ifndef __EMSCRIPTEN__
	auto *proxy = utils::addRadioGroup(
		form, tr("Network proxy:"), true,
		{{tr("System"), int(net::ProxyMode::Default)},
		 {tr("Disabled"), int(net::ProxyMode::Disabled)}});
	settings.bindNetworkProxyMode(proxy);
#endif

	auto *messageQueueDrainRate = new KisSliderSpinBox;
	messageQueueDrainRate->setRange(
		0, net::MessageQueue::MAX_SMOOTH_DRAIN_RATE);
	settings.bindMessageQueueDrainRate(messageQueueDrainRate);
	form->addRow(tr("Receive delay:"), messageQueueDrainRate);
	form->addRow(
		nullptr, utils::formNote(tr("The higher the value, the smoother "
									"strokes from other users come in.")));
	disableKineticScrollingOnWidget(messageQueueDrainRate);
}

} // namespace settingsdialog
} // namespace dialogs
