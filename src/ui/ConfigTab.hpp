/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#pragma once

#include "../core/EndpointRegistry.hpp"

#include <QtWidgets/QWidget>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QCheckBox>
#include <QtCore/QTimer>

namespace smulti {

class OutputController;

enum class OutputState : int;

class EndpointCard : public QWidget {
	Q_OBJECT

public:
	explicit EndpointCard(const Endpoint &ep, QWidget *parent = nullptr);

	void update_state(const Endpoint &ep);
	void update_runtime(OutputState state, const std::string &last_error);
	const std::string &endpoint_id() const { return m_id; }

signals:
	void editRequested(const std::string &id);
	void enableToggled(const std::string &id, bool enabled);
	void deleteRequested(const std::string &id);
	void manualStartStopRequested(const std::string &id);

private:
	void setup_ui();

	std::string m_id;
	Endpoint m_ep;

	QLabel *m_name_label {nullptr};
	QLabel *m_status_led {nullptr};
	QCheckBox *m_enabled_cb {nullptr};
	QPushButton *m_start_stop_btn {nullptr};
	QPushButton *m_edit_btn {nullptr};
	QPushButton *m_delete_btn {nullptr};
};

class ConfigTab : public QWidget {
	Q_OBJECT

public:
	explicit ConfigTab(EndpointRegistry &registry, QWidget *parent = nullptr);
	~ConfigTab() override;

private slots:
	void on_add_endpoint();
	void on_edit_endpoint(const std::string &id);
	void on_toggle_endpoint(const std::string &id, bool enabled);
	void on_delete_endpoint(const std::string &id);
	void on_manual_start_stop(const std::string &id);
	void on_list_reorder();
	void refresh_runtime_states();

private:
	void setup_ui();
	void rebuild_list();
	void on_registry_changed(ChangeKind kind, const Endpoint &ep);

	EndpointRegistry &m_registry;
	QListWidget *m_list {nullptr};
	QLabel *m_empty_label {nullptr};
	QPushButton *m_add_btn {nullptr};
	QTimer *m_runtime_timer {nullptr};

	int m_observer_token = -1;
};

} // namespace smulti
