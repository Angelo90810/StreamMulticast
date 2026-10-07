/*
StreamMulticast — Broadcast Hub tab
Copyright (C) 2026 Avanatro <contact@avanatro.com>
GPLv2 — see LICENSE for full text.
*/

#pragma once

#include "../core/EndpointRegistry.hpp"
#include "../core/HubConfig.hpp"
#include "../integrations/FacebookClient.hpp"

#include <QWidget>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>

namespace smulti {

class HubTab : public QWidget {
	Q_OBJECT

public:
	explicit HubTab(EndpointRegistry &registry, QWidget *parent = nullptr);

private slots:
	void save_plan();
	void refresh_youtube();
	void connect_facebook();
	void on_facebook_device_code(const QString &uri, const QString &code);
	void on_facebook_authenticated(const QString &user_token);
	void on_facebook_pages(const QJsonArray &pages);
	void on_facebook_page_changed(int index);
	void prepare_facebook();
	void publish_facebook();
	void on_facebook_live_created(const QString &id, const QString &secure_url);
	void on_facebook_error(const QString &message);

private:
	void setup_ui();
	void load_state();
	BroadcastPlan collect_plan() const;
	void save_state();
	void upsert_facebook_endpoint(const QString &secure_url);

	EndpointRegistry &m_registry;
	HubConfig m_config;
	FacebookClient m_facebook;

	QLineEdit *m_title_edit{nullptr};
	QPlainTextEdit *m_description_edit{nullptr};
	QDateTimeEdit *m_schedule_edit{nullptr};
	QComboBox *m_privacy_combo{nullptr};

	QLabel *m_youtube_status{nullptr};

	QLineEdit *m_meta_app_id{nullptr};
	QLineEdit *m_meta_client_token{nullptr};
	QLabel *m_facebook_status{nullptr};
	QComboBox *m_page_combo{nullptr};
	QPushButton *m_prepare_facebook_btn{nullptr};
	QPushButton *m_publish_facebook_btn{nullptr};
};

} // namespace smulti
