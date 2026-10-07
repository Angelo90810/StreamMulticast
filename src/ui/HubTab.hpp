/*
StreamMulticast — Broadcast Hub tab
Copyright (C) 2026 Avanatro <contact@avanatro.com>
GPLv2 — see LICENSE for full text.
*/

#pragma once

#include "../core/EndpointRegistry.hpp"
#include "../core/HubConfig.hpp"
#include "../integrations/FacebookClient.hpp"
#include "../integrations/YouTubeClient.hpp"

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
	void prepare_connected_destinations();

	void refresh_youtube();
	void connect_youtube();
	void prepare_youtube();
	void on_youtube_authenticated(const QString &access_token,
	                              const QString &refresh_token);
	void on_youtube_access_refreshed(const QString &access_token);
	void on_youtube_channel(const QString &id, const QString &name);
	void on_youtube_broadcast_prepared(const QString &broadcast_id,
	                                   const QString &stream_id);
	void on_youtube_error(const QString &message);

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
	bool youtube_native_config(std::string &stream_key, QString &service_name) const;

	EndpointRegistry &m_registry;
	HubConfig m_config;
	YouTubeClient m_youtube;
	FacebookClient m_facebook;
	QString m_youtube_access_token;

	QLineEdit *m_title_edit{nullptr};
	QPlainTextEdit *m_description_edit{nullptr};
	QDateTimeEdit *m_schedule_edit{nullptr};
	QComboBox *m_privacy_combo{nullptr};
	QPushButton *m_prepare_all_btn{nullptr};

	QLineEdit *m_google_client_id{nullptr};
	QLabel *m_youtube_status{nullptr};
	QPushButton *m_prepare_youtube_btn{nullptr};

	QLineEdit *m_meta_app_id{nullptr};
	QLineEdit *m_meta_client_token{nullptr};
	QLabel *m_facebook_status{nullptr};
	QComboBox *m_page_combo{nullptr};
	QPushButton *m_prepare_facebook_btn{nullptr};
	QPushButton *m_publish_facebook_btn{nullptr};
};

} // namespace smulti
