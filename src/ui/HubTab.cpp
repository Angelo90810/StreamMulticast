/*
StreamMulticast — Broadcast Hub tab
Copyright (C) 2026 Avanatro <contact@avanatro.com>
GPLv2 — see LICENSE for full text.
*/

#include "HubTab.hpp"
#include "../core/ObsServiceImport.hpp"
#include "../core/IngestUrl.hpp"

#include <QDesktopServices>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QMessageBox>
#include <QUrl>
#include <QVariantMap>
#include <QVBoxLayout>

namespace smulti {

HubTab::HubTab(EndpointRegistry &registry, QWidget *parent)
	: QWidget(parent)
	, m_registry(registry)
	, m_youtube(this)
	, m_facebook(this)
{
	m_config.load();
	setup_ui();
	load_state();

	connect(&m_youtube, &YouTubeClient::authorization_url_ready,
	        this, [](const QUrl &url) { QDesktopServices::openUrl(url); });
	connect(&m_youtube, &YouTubeClient::authenticated,
	        this, &HubTab::on_youtube_authenticated);
	connect(&m_youtube, &YouTubeClient::access_token_refreshed,
	        this, &HubTab::on_youtube_access_refreshed);
	connect(&m_youtube, &YouTubeClient::channel_ready,
	        this, &HubTab::on_youtube_channel);
	connect(&m_youtube, &YouTubeClient::broadcast_prepared,
	        this, &HubTab::on_youtube_broadcast_prepared);
	connect(&m_youtube, &YouTubeClient::error,
	        this, &HubTab::on_youtube_error);

	connect(&m_facebook, &FacebookClient::device_code_ready,
	        this, &HubTab::on_facebook_device_code);
	connect(&m_facebook, &FacebookClient::authenticated,
	        this, &HubTab::on_facebook_authenticated);
	connect(&m_facebook, &FacebookClient::pages_ready,
	        this, &HubTab::on_facebook_pages);
	connect(&m_facebook, &FacebookClient::live_created,
	        this, &HubTab::on_facebook_live_created);
	connect(&m_facebook, &FacebookClient::live_published,
	        this, [this]() {
		        m_facebook_status->setText(tr("✓ Facebook Live published."));
	        });
	connect(&m_facebook, &FacebookClient::error,
	        this, &HubTab::on_facebook_error);

	refresh_youtube();

	const HubState &state = m_config.state();
	if (!state.youtube_client_id.empty() && !state.youtube_refresh_token.empty()) {
		m_youtube_status->setText(tr("Refreshing saved YouTube authorization..."));
		m_youtube.refresh_access_token(
			QString::fromStdString(state.youtube_client_id),
			QString::fromStdString(state.youtube_refresh_token));
	}

	if (!state.facebook_user_token.empty())
		m_facebook.fetch_pages(QString::fromStdString(state.facebook_user_token));
}

void HubTab::setup_ui()
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(8, 8, 8, 8);
	outer->setSpacing(8);

	auto *plan_group = new QGroupBox(tr("Broadcast"), this);
	auto *plan_form = new QFormLayout(plan_group);

	m_title_edit = new QLineEdit(this);
	m_description_edit = new QPlainTextEdit(this);
	m_description_edit->setMaximumHeight(90);
	m_schedule_edit = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(300), this);
	m_schedule_edit->setCalendarPopup(true);
	m_schedule_edit->setDisplayFormat(QStringLiteral("dd/MM/yyyy HH:mm"));

	m_privacy_combo = new QComboBox(this);
	m_privacy_combo->addItem(tr("Public"), static_cast<int>(BroadcastPrivacy::Public));
	m_privacy_combo->addItem(tr("Unlisted"), static_cast<int>(BroadcastPrivacy::Unlisted));
	m_privacy_combo->addItem(tr("Private"), static_cast<int>(BroadcastPrivacy::Private));

	plan_form->addRow(tr("Title:"), m_title_edit);
	plan_form->addRow(tr("Description:"), m_description_edit);
	plan_form->addRow(tr("Scheduled start:"), m_schedule_edit);
	plan_form->addRow(tr("Privacy:"), m_privacy_combo);

	auto *plan_buttons = new QHBoxLayout();
	auto *save_btn = new QPushButton(tr("Save broadcast plan"), this);
	m_prepare_all_btn = new QPushButton(tr("Prepare YouTube + Facebook"), this);
	plan_buttons->addWidget(save_btn);
	plan_buttons->addWidget(m_prepare_all_btn);
	plan_form->addRow(QString(), plan_buttons);
	connect(save_btn, &QPushButton::clicked, this, &HubTab::save_plan);
	connect(m_prepare_all_btn, &QPushButton::clicked,
	        this, &HubTab::prepare_connected_destinations);
	outer->addWidget(plan_group);

	auto *youtube_group = new QGroupBox(tr("YouTube — OBS native video + Hub metadata"), this);
	auto *youtube_form = new QFormLayout(youtube_group);
	m_google_client_id = new QLineEdit(this);
	m_google_client_id->setPlaceholderText(tr("Desktop OAuth Client ID (*.apps.googleusercontent.com)"));
	auto *youtube_connect = new QPushButton(tr("Connect YouTube"), this);
	auto *youtube_refresh = new QPushButton(tr("Refresh OBS YouTube status"), this);
	m_prepare_youtube_btn = new QPushButton(tr("Prepare YouTube event"), this);
	m_youtube_status = new QLabel(this);
	m_youtube_status->setWordWrap(true);

	auto *youtube_buttons = new QHBoxLayout();
	youtube_buttons->addWidget(youtube_connect);
	youtube_buttons->addWidget(youtube_refresh);
	youtube_buttons->addWidget(m_prepare_youtube_btn);

	youtube_form->addRow(tr("Google Client ID:"), m_google_client_id);
	youtube_form->addRow(QString(), youtube_buttons);
	youtube_form->addRow(QString(), m_youtube_status);

	connect(youtube_connect, &QPushButton::clicked, this, &HubTab::connect_youtube);
	connect(youtube_refresh, &QPushButton::clicked, this, &HubTab::refresh_youtube);
	connect(m_prepare_youtube_btn, &QPushButton::clicked, this, &HubTab::prepare_youtube);
	outer->addWidget(youtube_group);

	auto *facebook_group = new QGroupBox(tr("Facebook — connected Page"), this);
	auto *facebook_form = new QFormLayout(facebook_group);
	m_meta_app_id = new QLineEdit(this);
	m_meta_app_id->setPlaceholderText(tr("Meta App ID"));
	m_meta_client_token = new QLineEdit(this);
	m_meta_client_token->setEchoMode(QLineEdit::Password);
	m_meta_client_token->setPlaceholderText(tr("Meta Client Token"));
	auto *connect_btn = new QPushButton(tr("Connect Facebook"), this);
	m_page_combo = new QComboBox(this);
	m_facebook_status = new QLabel(this);
	m_facebook_status->setWordWrap(true);

	auto *facebook_buttons = new QHBoxLayout();
	m_prepare_facebook_btn = new QPushButton(tr("Prepare Facebook Live"), this);
	m_publish_facebook_btn = new QPushButton(tr("Publish Facebook Live"), this);
	facebook_buttons->addWidget(m_prepare_facebook_btn);
	facebook_buttons->addWidget(m_publish_facebook_btn);

	facebook_form->addRow(tr("App ID:"), m_meta_app_id);
	facebook_form->addRow(tr("Client token:"), m_meta_client_token);
	facebook_form->addRow(QString(), connect_btn);
	facebook_form->addRow(tr("Page:"), m_page_combo);
	facebook_form->addRow(QString(), facebook_buttons);
	facebook_form->addRow(QString(), m_facebook_status);

	connect(connect_btn, &QPushButton::clicked, this, &HubTab::connect_facebook);
	connect(m_page_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &HubTab::on_facebook_page_changed);
	connect(m_prepare_facebook_btn, &QPushButton::clicked,
	        this, &HubTab::prepare_facebook);
	connect(m_publish_facebook_btn, &QPushButton::clicked,
	        this, &HubTab::publish_facebook);
	outer->addWidget(facebook_group);

	auto *instagram_group = new QGroupBox(tr("Instagram — manual"), this);
	auto *instagram_layout = new QVBoxLayout(instagram_group);
	auto *instagram_text = new QLabel(
		tr("Instagram stays manual by design. Create the Live in Instagram Live Producer, "
		   "copy the temporary stream key, then use the Instagram endpoint in Configure."),
		this);
	instagram_text->setWordWrap(true);
	instagram_layout->addWidget(instagram_text);
	outer->addWidget(instagram_group);

	outer->addStretch();
}

void HubTab::load_state()
{
	const HubState &state = m_config.state();
	m_title_edit->setText(QString::fromStdString(state.plan.title));
	m_description_edit->setPlainText(QString::fromStdString(state.plan.description));

	if (!state.plan.scheduled_start_utc.empty()) {
		const QDateTime dt = QDateTime::fromString(
			QString::fromStdString(state.plan.scheduled_start_utc), Qt::ISODate);
		if (dt.isValid())
			m_schedule_edit->setDateTime(dt.toLocalTime());
	}

	for (int i = 0; i < m_privacy_combo->count(); ++i) {
		if (m_privacy_combo->itemData(i).toInt() ==
		    static_cast<int>(state.plan.privacy)) {
			m_privacy_combo->setCurrentIndex(i);
			break;
		}
	}

	m_google_client_id->setText(QString::fromStdString(state.youtube_client_id));
	if (!state.youtube_channel_name.empty())
		m_youtube_status->setText(
			tr("Saved channel: %1").arg(QString::fromStdString(state.youtube_channel_name)));

	m_meta_app_id->setText(QString::fromStdString(state.meta_app_id));
	m_meta_client_token->setText(QString::fromStdString(state.meta_client_token));

	if (!state.facebook_page_name.empty())
		m_facebook_status->setText(
			tr("Saved Page: %1").arg(QString::fromStdString(state.facebook_page_name)));

	m_publish_facebook_btn->setEnabled(!state.facebook_live_video_id.empty());
}

BroadcastPlan HubTab::collect_plan() const
{
	BroadcastPlan plan;
	plan.title = m_title_edit->text().trimmed().toStdString();
	plan.description = m_description_edit->toPlainText().trimmed().toStdString();
	plan.scheduled_start_utc =
		m_schedule_edit->dateTime().toUTC().toString(Qt::ISODate).toStdString();
	plan.privacy = static_cast<BroadcastPrivacy>(m_privacy_combo->currentData().toInt());
	return plan;
}

void HubTab::save_state()
{
	HubState state = m_config.state();
	state.plan = collect_plan();
	state.youtube_client_id = m_google_client_id->text().trimmed().toStdString();
	state.meta_app_id = m_meta_app_id->text().trimmed().toStdString();
	state.meta_client_token = m_meta_client_token->text().toStdString();
	m_config.save(state);
}

void HubTab::save_plan()
{
	save_state();
	m_youtube_status->setText(tr("Broadcast plan saved."));
	m_facebook_status->setText(tr("Broadcast plan saved."));
}

bool HubTab::youtube_native_config(std::string &stream_key, QString &service_name) const
{
	const ObsServiceConfig cfg = import_from_active_obs_profile();
	service_name = QString::fromStdString(cfg.service_name);
	if (!cfg.ok || !service_name.contains(QStringLiteral("YouTube"), Qt::CaseInsensitive))
		return false;

	stream_key = cfg.stream_key;
	return !stream_key.empty();
}

void HubTab::refresh_youtube()
{
	std::string key;
	QString service;
	if (youtube_native_config(key, service)) {
		const QString channel = QString::fromStdString(m_config.state().youtube_channel_name);
		m_youtube_status->setText(
			channel.isEmpty()
				? tr("✓ YouTube is the native OBS destination (%1). Connect the Hub account to manage events.")
					.arg(service)
				: tr("✓ OBS native: %1 | Hub channel: %2. No duplicate YouTube output will be created.")
					.arg(service, channel));
		m_youtube_status->setStyleSheet(QStringLiteral("color: #2ecc71;"));
	} else {
		m_youtube_status->setText(
			tr("YouTube is not currently detected as the native OBS destination. "
			   "Configure YouTube in OBS Settings → Stream."));
		m_youtube_status->setStyleSheet(QStringLiteral("color: #f39c12;"));
	}
}

void HubTab::connect_youtube()
{
	save_state();
	if (m_google_client_id->text().trimmed().isEmpty()) {
		QMessageBox::warning(
			this, tr("Broadcast Hub"),
			tr("Enter a Google Desktop OAuth Client ID first."));
		return;
	}

	m_youtube_status->setStyleSheet(QString());
	m_youtube_status->setText(
		tr("Opening Google authorization. The callback returns only to this PC (127.0.0.1)."));
	m_youtube.start_oauth(m_google_client_id->text());
}

void HubTab::on_youtube_authenticated(const QString &access_token,
                                      const QString &refresh_token)
{
	m_youtube_access_token = access_token;

	HubState state = m_config.state();
	if (!refresh_token.isEmpty())
		state.youtube_refresh_token = refresh_token.toStdString();
	state.youtube_client_id = m_google_client_id->text().trimmed().toStdString();
	m_config.save(state);

	m_youtube_status->setText(tr("✓ YouTube authorized. Loading channel..."));
	m_youtube.fetch_channel(access_token);
}

void HubTab::on_youtube_access_refreshed(const QString &access_token)
{
	m_youtube_access_token = access_token;

	if (m_youtube_prepare_pending) {
		m_youtube_prepare_pending = false;
		prepare_youtube_with_current_token();
		return;
	}

	m_youtube_status->setText(tr("✓ YouTube authorization restored. Loading channel..."));
	m_youtube.fetch_channel(access_token);
}

void HubTab::on_youtube_channel(const QString &id, const QString &name)
{
	HubState state = m_config.state();
	state.youtube_channel_id = id.toStdString();
	state.youtube_channel_name = name.toStdString();
	m_config.save(state);

	m_youtube_status->setStyleSheet(QStringLiteral("color: #2ecc71;"));
	refresh_youtube();
}

void HubTab::prepare_youtube()
{
	save_state();

	const HubState &state = m_config.state();
	if (state.plan.title.empty()) {
		QMessageBox::warning(
			this, tr("Broadcast Hub"),
			tr("Enter a broadcast title first."));
		return;
	}

	if (!state.youtube_refresh_token.empty() && !state.youtube_client_id.empty()) {
		request_fresh_youtube_token_for_prepare();
		return;
	}

	if (m_youtube_access_token.isEmpty()) {
		QMessageBox::warning(
			this, tr("Broadcast Hub"),
			tr("Connect YouTube in the Hub first."));
		return;
	}

	prepare_youtube_with_current_token();
}

void HubTab::request_fresh_youtube_token_for_prepare()
{
	const HubState &state = m_config.state();
	m_youtube_prepare_pending = true;
	m_youtube_status->setStyleSheet(QString());
	m_youtube_status->setText(tr("Refreshing YouTube authorization before preparing the event..."));
	m_youtube.refresh_access_token(
		QString::fromStdString(state.youtube_client_id),
		QString::fromStdString(state.youtube_refresh_token));
}

void HubTab::prepare_youtube_with_current_token()
{
	if (m_youtube_access_token.isEmpty()) {
		QMessageBox::warning(
			this, tr("Broadcast Hub"),
			tr("Connect YouTube in the Hub first."));
		return;
	}

	std::string stream_key;
	QString service;
	if (!youtube_native_config(stream_key, service)) {
		QMessageBox::warning(
			this, tr("Broadcast Hub"),
			tr("YouTube must be configured as the native OBS streaming service first."));
		return;
	}

	m_youtube_status->setStyleSheet(QString());
	m_youtube_status->setText(
		tr("Finding the reusable YouTube stream already configured in OBS..."));
	m_youtube.prepare_native_obs_broadcast(
		m_youtube_access_token,
		QString::fromStdString(stream_key),
		m_config.state().plan);
}

void HubTab::on_youtube_broadcast_prepared(const QString &broadcast_id,
                                           const QString &stream_id)
{
	HubState state = m_config.state();
	state.youtube_broadcast_id = broadcast_id.toStdString();
	m_config.save(state);

	m_youtube_status->setStyleSheet(QStringLiteral("color: #2ecc71;"));
	m_youtube_status->setText(
		tr("✓ YouTube event prepared and bound to the OBS-native stream. "
		   "Auto-start/auto-stop are enabled. Broadcast ID: %1 | Stream: %2")
			.arg(broadcast_id, stream_id));
}

void HubTab::on_youtube_error(const QString &message)
{
	m_youtube_prepare_pending = false;
	m_youtube_status->setText(tr("YouTube error: %1").arg(message));
	m_youtube_status->setStyleSheet(QStringLiteral("color: #e74c3c;"));
}

void HubTab::prepare_connected_destinations()
{
	save_state();
	if (m_config.state().plan.title.empty()) {
		QMessageBox::warning(this, tr("Broadcast Hub"), tr("Enter a broadcast title first."));
		return;
	}

	bool attempted = false;
	const HubState &state = m_config.state();

	if (!state.youtube_refresh_token.empty() && !state.youtube_client_id.empty()) {
		attempted = true;
		request_fresh_youtube_token_for_prepare();
	} else if (!m_youtube_access_token.isEmpty()) {
		std::string stream_key;
		QString service;
		if (youtube_native_config(stream_key, service)) {
			attempted = true;
			m_youtube_status->setText(tr("Preparing YouTube..."));
			m_youtube.prepare_native_obs_broadcast(
				m_youtube_access_token,
				QString::fromStdString(stream_key),
				state.plan);
		}
	}


	if (!state.facebook_page_id.empty() && !state.facebook_page_token.empty()) {
		attempted = true;
		m_facebook_status->setText(tr("Preparing Facebook..."));
		m_facebook.create_live(
			QString::fromStdString(state.facebook_page_id),
			QString::fromStdString(state.facebook_page_token),
			state.plan);
	}

	if (!attempted) {
		QMessageBox::information(
			this, tr("Broadcast Hub"),
			tr("Connect YouTube and/or Facebook before preparing destinations."));
	}
}

void HubTab::connect_facebook()
{
	save_state();
	m_facebook_status->setText(tr("Starting Facebook device login..."));
	m_facebook.start_device_login(m_meta_app_id->text(), m_meta_client_token->text());
}

void HubTab::on_facebook_device_code(const QString &uri, const QString &code)
{
	m_facebook_status->setText(
		tr("Facebook authorization opened in your browser. Code: %1").arg(code));
	QDesktopServices::openUrl(QUrl(uri));
}

void HubTab::on_facebook_authenticated(const QString &user_token)
{
	HubState state = m_config.state();
	state.facebook_user_token = user_token.toStdString();
	m_config.save(state);

	m_facebook_status->setText(tr("✓ Facebook connected. Loading managed Pages..."));
	m_facebook.fetch_pages(user_token);
}

void HubTab::on_facebook_pages(const QJsonArray &pages)
{
	m_page_combo->blockSignals(true);
	m_page_combo->clear();

	for (const auto &value : pages) {
		const QJsonObject page = value.toObject();
		const QString id = page.value(QStringLiteral("id")).toVariant().toString();
		const QString name = page.value(QStringLiteral("name")).toString();
		const QString token = page.value(QStringLiteral("access_token")).toString();
		if (id.isEmpty() || name.isEmpty() || token.isEmpty())
			continue;

		QVariantMap data;
		data.insert(QStringLiteral("id"), id);
		data.insert(QStringLiteral("name"), name);
		data.insert(QStringLiteral("token"), token);
		m_page_combo->addItem(name, data);
	}

	const std::string saved_id = m_config.state().facebook_page_id;
	for (int i = 0; i < m_page_combo->count(); ++i) {
		const QVariantMap data = m_page_combo->itemData(i).toMap();
		if (data.value(QStringLiteral("id")).toString().toStdString() == saved_id) {
			m_page_combo->setCurrentIndex(i);
			break;
		}
	}
	m_page_combo->blockSignals(false);

	if (m_page_combo->count() > 0) {
		on_facebook_page_changed(m_page_combo->currentIndex());
		m_facebook_status->setText(
			tr("✓ Facebook connected. %1 Page(s) available.").arg(m_page_combo->count()));
	} else {
		m_facebook_status->setText(
			tr("Facebook connected, but no manageable Page was returned."));
	}
}

void HubTab::on_facebook_page_changed(int index)
{
	if (index < 0)
		return;

	const QVariantMap data = m_page_combo->itemData(index).toMap();
	HubState state = m_config.state();
	state.facebook_page_id = data.value(QStringLiteral("id")).toString().toStdString();
	state.facebook_page_name = data.value(QStringLiteral("name")).toString().toStdString();
	state.facebook_page_token = data.value(QStringLiteral("token")).toString().toStdString();
	m_config.save(state);
}

void HubTab::prepare_facebook()
{
	save_state();
	const HubState &state = m_config.state();

	if (state.plan.title.empty()) {
		QMessageBox::warning(this, tr("Broadcast Hub"), tr("Enter a broadcast title first."));
		return;
	}
	if (state.facebook_page_id.empty() || state.facebook_page_token.empty()) {
		QMessageBox::warning(this, tr("Broadcast Hub"), tr("Connect Facebook and select a Page first."));
		return;
	}

	m_facebook_status->setText(tr("Creating unpublished Facebook Live..."));
	m_facebook.create_live(
		QString::fromStdString(state.facebook_page_id),
		QString::fromStdString(state.facebook_page_token),
		state.plan);
}

void HubTab::publish_facebook()
{
	const HubState &state = m_config.state();
	m_facebook.publish_live(
		QString::fromStdString(state.facebook_live_video_id),
		QString::fromStdString(state.facebook_page_token));
}

void HubTab::upsert_facebook_endpoint(const QString &secure_url)
{
	const SplitIngestUrl split =
		split_rtmp_ingest_url(secure_url.toStdString());
	if (!split.ok) {
		on_facebook_error(tr("Facebook returned an ingest URL that could not be split into server + stream key."));
		return;
	}

	const QString server = QString::fromStdString(split.server_url);
	const QString key = QString::fromStdString(split.stream_key);

	auto endpoints = m_registry.all();
	for (const auto &ep : endpoints) {
		if (ep.name == "Facebook (Hub)") {
			Endpoint updated = ep;
			updated.server_url = server.toStdString();
			updated.stream_key = key.toStdString();
			updated.video_codec = VideoCodec::H264;
			updated.video_settings_mode = EncoderSettingsMode::Custom;
			updated.audio_settings_mode = EncoderSettingsMode::Custom;
			updated.keyframe_interval_sec = 2;
			updated.linked_to_main = true;
			updated.enabled = true;
			m_registry.update(updated);
			return;
		}
	}

	Endpoint ep = Endpoint::make_default("Facebook (Hub)");
	ep.server_url = server.toStdString();
	ep.stream_key = key.toStdString();
	ep.video_codec = VideoCodec::H264;
	ep.video_settings_mode = EncoderSettingsMode::Custom;
	ep.audio_settings_mode = EncoderSettingsMode::Custom;
	ep.video_bitrate_kbps = 6000;
	ep.audio_bitrate_kbps = 160;
	ep.keyframe_interval_sec = 2;
	ep.orientation = OutputOrientation::SourceMatch;
	ep.linked_to_main = true;
	m_registry.add(ep);
}

void HubTab::on_facebook_live_created(const QString &id, const QString &secure_url)
{
	HubState state = m_config.state();
	state.facebook_live_video_id = id.toStdString();
	m_config.save(state);

	upsert_facebook_endpoint(secure_url);
	m_publish_facebook_btn->setEnabled(true);
	m_facebook_status->setText(
		tr("✓ Facebook Live prepared and endpoint configured. "
		   "Start OBS to send video, then publish when the preview is healthy."));
}

void HubTab::on_facebook_error(const QString &message)
{
	m_facebook_status->setText(tr("Facebook error: %1").arg(message));
	m_facebook_status->setStyleSheet(QStringLiteral("color: #e74c3c;"));
}

} // namespace smulti
