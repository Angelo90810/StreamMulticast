/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#include "ConfigTab.hpp"
#include "EndpointDialog.hpp"
#include "../pipeline/OutputController.hpp"

#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QListWidgetItem>
#include <QtCore/QMetaObject>
#include <obs-frontend-api.h>

namespace smulti {

EndpointCard::EndpointCard(const Endpoint &ep, QWidget *parent)
	: QWidget(parent), m_id(ep.id), m_ep(ep)
{
	setup_ui();
	update_state(ep);
	update_runtime(OutputState::Idle, {});
}

void EndpointCard::setup_ui()
{
	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(8, 6, 8, 6);
	layout->setSpacing(8);

	m_status_led = new QLabel(this);
	m_status_led->setFixedSize(14, 14);
	layout->addWidget(m_status_led);

	m_name_label = new QLabel(this);
	m_name_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	layout->addWidget(m_name_label, 1);

	m_enabled_cb = new QCheckBox(tr("On"), this);
	layout->addWidget(m_enabled_cb);

	m_start_stop_btn = new QPushButton(tr("Start"), this);
	m_start_stop_btn->setMinimumWidth(72);
	m_start_stop_btn->setMinimumHeight(28);
	m_start_stop_btn->setToolTip(tr("Start or stop this endpoint independently"));
	layout->addWidget(m_start_stop_btn);

	m_edit_btn = new QPushButton(tr("Edit"), this);
	m_edit_btn->setMinimumWidth(72);
	m_edit_btn->setMinimumHeight(28);
	layout->addWidget(m_edit_btn);

	m_delete_btn = new QPushButton(tr("Delete"), this);
	m_delete_btn->setMinimumWidth(72);
	m_delete_btn->setMinimumHeight(28);
	layout->addWidget(m_delete_btn);

	setMinimumHeight(44);

	connect(m_edit_btn, &QPushButton::clicked, this, [this]() {
		emit editRequested(m_id);
	});
	connect(m_enabled_cb, &QCheckBox::toggled, this, [this](bool checked) {
		emit enableToggled(m_id, checked);
	});
	connect(m_delete_btn, &QPushButton::clicked, this, [this]() {
		emit deleteRequested(m_id);
	});
	connect(m_start_stop_btn, &QPushButton::clicked, this, [this]() {
		emit manualStartStopRequested(m_id);
	});
}

void EndpointCard::update_state(const Endpoint &ep)
{
	m_ep = ep;
	m_name_label->setText(QString::fromStdString(ep.name));

	m_enabled_cb->blockSignals(true);
	m_enabled_cb->setChecked(ep.enabled);
	m_enabled_cb->blockSignals(false);

	/* Manual control is intentionally exposed only when automatic linkage is
	 * disabled. This keeps one obvious source of truth for start/stop. */
	m_start_stop_btn->setVisible(!ep.linked_to_main);
	m_start_stop_btn->setEnabled(ep.enabled);
	m_start_stop_btn->setToolTip(
		ep.linked_to_main
			? tr("Automatic start/stop is enabled for this endpoint")
			: tr("Start or stop this endpoint independently"));
}

void EndpointCard::update_runtime(OutputState state, const std::string &last_error)
{
	QString led = "background: #95a5a6; border-radius: 7px;";
	QString text = tr("Start");
	bool can_click = m_ep.enabled;

	switch (state) {
	case OutputState::Starting:
		led = "background: #f1c40f; border-radius: 7px;";
		text = tr("Stop");
		break;
	case OutputState::Live:
		led = "background: #2ecc71; border-radius: 7px;";
		text = tr("Stop");
		break;
	case OutputState::Reconnecting:
		led = "background: #e67e22; border-radius: 7px;";
		text = tr("Stop");
		break;
	case OutputState::FailedHard:
		led = "background: #e74c3c; border-radius: 7px;";
		text = tr("Start");
		break;
	case OutputState::Stopping:
		led = "background: #7f8c8d; border-radius: 7px;";
		text = tr("Stopping...");
		can_click = false;
		break;
	case OutputState::Idle:
	default:
		break;
	}

	m_status_led->setStyleSheet(led);
	m_start_stop_btn->setText(text);
	m_start_stop_btn->setEnabled(can_click);

	if (!last_error.empty()) {
		QString error = QString::fromStdString(last_error);
		m_status_led->setToolTip(error);
		m_start_stop_btn->setToolTip(error);
	} else {
		m_status_led->setToolTip(QString());
		m_start_stop_btn->setToolTip(
			m_ep.linked_to_main
				? tr("Automatic start/stop is enabled for this endpoint")
				: tr("Start or stop this endpoint independently"));
	}
}

ConfigTab::ConfigTab(EndpointRegistry &registry, QWidget *parent)
	: QWidget(parent), m_registry(registry)
{
	setup_ui();

	m_observer_token = m_registry.register_observer(
		[this](ChangeKind kind, const Endpoint &ep) {
			QMetaObject::invokeMethod(
				this,
				[this, kind, ep]() { on_registry_changed(kind, ep); },
				Qt::QueuedConnection);
		});

	m_runtime_timer = new QTimer(this);
	m_runtime_timer->setInterval(350);
	connect(m_runtime_timer, &QTimer::timeout, this, &ConfigTab::refresh_runtime_states);
	m_runtime_timer->start();
	refresh_runtime_states();
}

ConfigTab::~ConfigTab()
{
	if (m_observer_token >= 0)
		m_registry.unregister_observer(m_observer_token);
}

void ConfigTab::setup_ui()
{
	auto *outer_layout = new QVBoxLayout(this);
	outer_layout->setContentsMargins(4, 4, 4, 4);
	outer_layout->setSpacing(4);

	m_list = new QListWidget(this);
	m_list->setDragDropMode(QAbstractItemView::InternalMove);
	m_list->setDefaultDropAction(Qt::MoveAction);
	m_list->setSelectionMode(QAbstractItemView::NoSelection);
	m_list->setSpacing(2);
	outer_layout->addWidget(m_list, 1);

	m_add_btn = new QPushButton(tr("+ Add Endpoint"), this);
	outer_layout->addWidget(m_add_btn);

	connect(m_add_btn, &QPushButton::clicked, this, &ConfigTab::on_add_endpoint);
	connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, &ConfigTab::on_list_reorder);

	rebuild_list();
}

void ConfigTab::rebuild_list()
{
	m_list->clear();

	for (const auto &ep : m_registry.all()) {
		auto *card = new EndpointCard(ep, nullptr);
		auto *item = new QListWidgetItem();
		item->setSizeHint(card->sizeHint());
		item->setData(Qt::UserRole, QString::fromStdString(ep.id));
		m_list->addItem(item);
		m_list->setItemWidget(item, card);

		connect(card, &EndpointCard::editRequested, this, &ConfigTab::on_edit_endpoint);
		connect(card, &EndpointCard::enableToggled, this, &ConfigTab::on_toggle_endpoint);
		connect(card, &EndpointCard::deleteRequested, this, &ConfigTab::on_delete_endpoint);
		connect(card, &EndpointCard::manualStartStopRequested,
		        this, &ConfigTab::on_manual_start_stop);
	}
	refresh_runtime_states();
}

void ConfigTab::on_add_endpoint()
{
	Endpoint new_ep = Endpoint::make_default("New Endpoint");
	EndpointDialog dlg(new_ep, m_registry, this);
	if (dlg.exec() == QDialog::Accepted)
		m_registry.add(dlg.result_endpoint());
}

void ConfigTab::on_edit_endpoint(const std::string &id)
{
	auto ep = m_registry.find(id);
	if (!ep)
		return;

	auto before_ctrl = m_registry.controller_for(id);
	const bool was_active = before_ctrl && before_ctrl->has_active_session();

	EndpointDialog dlg(*ep, m_registry, this);
	if (dlg.exec() != QDialog::Accepted)
		return;

	const Endpoint result = dlg.result_endpoint();
	m_registry.update(result);

	auto after_ctrl = m_registry.controller_for(id);
	if (!after_ctrl || !result.enabled)
		return;

	/* If runtime settings changed while this endpoint was live, Registry
	 * replaces the controller and gates it until old teardown completes.
	 * Preserve the user's previous live intent without an unload-unsafe
	 * queued callback. */
	if (was_active && after_ctrl != before_ctrl) {
		m_pending_starts.insert(id);
		return;
	}

	/* Turning on linked-to-main while OBS is already streaming must take
	 * effect immediately; there will be no new STREAMING_STARTED event. */
	if (result.linked_to_main && obs_frontend_streaming_active() &&
	    !after_ctrl->has_active_session()) {
		m_pending_starts.insert(id);
	}
}

void ConfigTab::on_toggle_endpoint(const std::string &id, bool enabled)
{
	auto ep = m_registry.find(id);
	if (!ep)
		return;

	Endpoint updated = *ep;
	updated.enabled = enabled;
	m_registry.update(updated);

	if (!enabled) {
		m_pending_starts.erase(id);
		return;
	}

	/* Enabling an auto-linked endpoint while the OBS stream is already live
	 * should start it as soon as any previous asynchronous stop finishes. */
	if (updated.linked_to_main && obs_frontend_streaming_active())
		m_pending_starts.insert(id);
}

void ConfigTab::on_manual_start_stop(const std::string &id)
{
	auto ep = m_registry.find(id);
	if (!ep || !ep->enabled || ep->linked_to_main)
		return;

	auto ctrl = m_registry.controller_for(id);
	if (!ctrl)
		return;

	OutputState state = ctrl->state();
	if (state == OutputState::Idle || state == OutputState::FailedHard)
		ctrl->start();
	else
		ctrl->stop();

	refresh_runtime_states();
}

void ConfigTab::on_delete_endpoint(const std::string &id)
{
	auto ep = m_registry.find(id);
	if (!ep)
		return;

	auto reply = QMessageBox::question(
		this,
		tr("Remove Endpoint"),
		tr("Remove endpoint \"%1\"?").arg(QString::fromStdString(ep->name)),
		QMessageBox::Yes | QMessageBox::No);
	if (reply == QMessageBox::Yes)
		m_registry.remove(id);
}

void ConfigTab::on_list_reorder()
{
	std::vector<std::string> ordered_ids;
	ordered_ids.reserve(m_list->count());
	for (int i = 0; i < m_list->count(); ++i)
		ordered_ids.push_back(m_list->item(i)->data(Qt::UserRole).toString().toStdString());
	m_registry.reorder(ordered_ids);
}

void ConfigTab::refresh_runtime_states()
{
	/* Resolve deferred starts on the Qt thread. The controller's handoff gate
	 * becomes Idle only after ControllerReaper has fully destroyed older
	 * outputs, so this cannot briefly double-stream after a live edit. */
	for (auto it = m_pending_starts.begin(); it != m_pending_starts.end();) {
		auto ep = m_registry.find(*it);
		auto ctrl = m_registry.controller_for(*it);

		if (!ep || !ctrl || !ep->enabled) {
			it = m_pending_starts.erase(it);
			continue;
		}

		if (ctrl->start_blocked() || ctrl->state() == OutputState::Stopping) {
			++it;
			continue;
		}

		if (ctrl->has_active_session()) {
			it = m_pending_starts.erase(it);
			continue;
		}

		const bool should_start =
			!ep->linked_to_main || obs_frontend_streaming_active();

		if (ctrl->state() == OutputState::Idle && should_start)
			ctrl->start();

		/* One attempt per intent. A real failure remains visible instead of
		 * the timer hammering the platform every 350 ms. */
		it = m_pending_starts.erase(it);
	}

	for (int i = 0; i < m_list->count(); ++i) {
		auto *item = m_list->item(i);
		auto *card = qobject_cast<EndpointCard *>(m_list->itemWidget(item));
		if (!card)
			continue;

		auto ctrl = m_registry.controller_for(card->endpoint_id());
		if (ctrl)
			card->update_runtime(ctrl->state(), ctrl->last_error());
		else
			card->update_runtime(OutputState::Idle, {});
	}
}

void ConfigTab::on_registry_changed(ChangeKind kind, const Endpoint &ep)
{
	if (kind == ChangeKind::Added || kind == ChangeKind::Removed) {
		rebuild_list();
		return;
	}

	for (int i = 0; i < m_list->count(); ++i) {
		auto *item = m_list->item(i);
		if (item->data(Qt::UserRole).toString().toStdString() != ep.id)
			continue;
		auto *card = qobject_cast<EndpointCard *>(m_list->itemWidget(item));
		if (card)
			card->update_state(ep);
		break;
	}
	refresh_runtime_states();
}

} // namespace smulti
