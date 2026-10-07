/*
StreamMulticast — Facebook Graph API integration for Broadcast Hub
Copyright (C) 2026 Avanatro <contact@avanatro.com>
GPLv2 — see LICENSE for full text.
*/

#include "FacebookClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace smulti {

namespace {
constexpr const char *GRAPH_BASE = "https://graph.facebook.com/v26.0";
}

FacebookClient::FacebookClient(QObject *parent)
	: QObject(parent)
{
	m_poll_timer.setSingleShot(false);
	connect(&m_poll_timer, &QTimer::timeout, this, &FacebookClient::poll_device_login);
}

QNetworkReply *FacebookClient::post_form(const QUrl &url, const QUrlQuery &form)
{
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader,
	                  QStringLiteral("application/x-www-form-urlencoded"));
	return m_network.post(request, form.query(QUrl::FullyEncoded).toUtf8());
}

void FacebookClient::emit_graph_error(const QByteArray &payload, const QString &fallback)
{
	const QJsonObject root = QJsonDocument::fromJson(payload).object();
	const QJsonObject error_obj = root.value(QStringLiteral("error")).toObject();
	QString message = error_obj.value(QStringLiteral("message")).toString();
	if (message.isEmpty())
		message = fallback;
	emit error(message);
}

void FacebookClient::start_device_login(const QString &app_id, const QString &client_token)
{
	m_poll_timer.stop();
	m_device_code.clear();

	if (app_id.trimmed().isEmpty() || client_token.trimmed().isEmpty()) {
		emit error(tr("Meta App ID and Client Token are required."));
		return;
	}

	m_client_access_token = app_id.trimmed() + QStringLiteral("|") + client_token.trimmed();

	QUrlQuery form;
	form.addQueryItem(QStringLiteral("access_token"), m_client_access_token);
	form.addQueryItem(
		QStringLiteral("scope"),
		QStringLiteral("public_profile,pages_show_list,pages_read_engagement,pages_manage_posts,publish_video"));

	auto *reply = post_form(QUrl(QString::fromLatin1(GRAPH_BASE) + QStringLiteral("/device/login")), form);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const QByteArray payload = reply->readAll();
		reply->deleteLater();

		const QJsonObject obj = QJsonDocument::fromJson(payload).object();
		m_device_code = obj.value(QStringLiteral("code")).toString();
		const QString user_code = obj.value(QStringLiteral("user_code")).toString();
		const QString verification_uri = obj.value(QStringLiteral("verification_uri")).toString();
		const int interval = qMax(5, obj.value(QStringLiteral("interval")).toInt(5));

		if (m_device_code.isEmpty() || user_code.isEmpty() || verification_uri.isEmpty()) {
			emit_graph_error(payload, tr("Facebook device login could not be started."));
			return;
		}

		emit device_code_ready(verification_uri, user_code);
		m_poll_timer.start(interval * 1000);
	});
}

void FacebookClient::poll_device_login()
{
	if (m_device_code.isEmpty() || m_client_access_token.isEmpty()) {
		m_poll_timer.stop();
		return;
	}

	QUrlQuery form;
	form.addQueryItem(QStringLiteral("access_token"), m_client_access_token);
	form.addQueryItem(QStringLiteral("code"), m_device_code);

	auto *reply = post_form(
		QUrl(QString::fromLatin1(GRAPH_BASE) + QStringLiteral("/device/login_status")), form);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const QByteArray payload = reply->readAll();
		reply->deleteLater();

		const QJsonObject obj = QJsonDocument::fromJson(payload).object();
		const QString token = obj.value(QStringLiteral("access_token")).toString();
		if (!token.isEmpty()) {
			m_poll_timer.stop();
			m_device_code.clear();
			emit authenticated(token);
			return;
		}

		const QJsonObject error_obj = obj.value(QStringLiteral("error")).toObject();
		const int subcode = error_obj.value(QStringLiteral("error_subcode")).toInt();
		/* 1349174 is the documented/long-standing "authorization pending"
		 * result for Facebook Device Login. Keep polling only in that case. */
		if (subcode == 1349174)
			return;

		m_poll_timer.stop();
		emit_graph_error(payload, tr("Facebook login failed."));
	});
}

void FacebookClient::fetch_pages(const QString &user_token)
{
	QUrl url(QString::fromLatin1(GRAPH_BASE) + QStringLiteral("/me/accounts"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name,access_token"));
	query.addQueryItem(QStringLiteral("access_token"), user_token);
	url.setQuery(query);

	auto *reply = m_network.get(QNetworkRequest(url));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const QByteArray payload = reply->readAll();
		reply->deleteLater();

		const QJsonObject root = QJsonDocument::fromJson(payload).object();
		if (root.contains(QStringLiteral("error"))) {
			emit_graph_error(payload, tr("Could not load Facebook Pages."));
			return;
		}

		emit pages_ready(root.value(QStringLiteral("data")).toArray());
	});
}

void FacebookClient::create_live(const QString &page_id,
                                 const QString &page_token,
                                 const BroadcastPlan &plan)
{
	if (page_id.isEmpty() || page_token.isEmpty() || plan.title.empty()) {
		emit error(tr("Select a Facebook Page and enter a broadcast title first."));
		return;
	}

	QUrlQuery form;
	form.addQueryItem(QStringLiteral("access_token"), page_token);
	form.addQueryItem(QStringLiteral("title"), QString::fromStdString(plan.title));
	form.addQueryItem(QStringLiteral("description"), QString::fromStdString(plan.description));

	/* Prepare safely by default. An unpublished live accepts encoder input
	 * without exposing a half-ready broadcast to viewers. */
	form.addQueryItem(QStringLiteral("status"), QStringLiteral("UNPUBLISHED"));

	auto *reply = post_form(
		QUrl(QString::fromLatin1(GRAPH_BASE) + QStringLiteral("/") + page_id +
		     QStringLiteral("/live_videos?fields=id,secure_stream_url,stream_url")),
		form);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const QByteArray payload = reply->readAll();
		reply->deleteLater();

		const QJsonObject root = QJsonDocument::fromJson(payload).object();
		if (root.contains(QStringLiteral("error"))) {
			emit_graph_error(payload, tr("Could not create the Facebook Live."));
			return;
		}

		const QString id = root.value(QStringLiteral("id")).toVariant().toString();
		QString secure_url = root.value(QStringLiteral("secure_stream_url")).toString();
		if (secure_url.isEmpty())
			secure_url = root.value(QStringLiteral("stream_url")).toString();

		if (id.isEmpty() || secure_url.isEmpty()) {
			emit error(tr("Facebook created the Live but did not return an ingest URL."));
			return;
		}

		emit live_created(id, secure_url);
	});
}

void FacebookClient::publish_live(const QString &live_video_id,
                                  const QString &page_token)
{
	if (live_video_id.isEmpty() || page_token.isEmpty()) {
		emit error(tr("Prepare a Facebook Live first."));
		return;
	}

	QUrlQuery form;
	form.addQueryItem(QStringLiteral("access_token"), page_token);
	form.addQueryItem(QStringLiteral("status"), QStringLiteral("LIVE_NOW"));

	auto *reply = post_form(
		QUrl(QString::fromLatin1(GRAPH_BASE) + QStringLiteral("/") + live_video_id), form);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const QByteArray payload = reply->readAll();
		reply->deleteLater();

		const QJsonObject root = QJsonDocument::fromJson(payload).object();
		if (root.contains(QStringLiteral("error"))) {
			emit_graph_error(payload, tr("Could not publish the Facebook Live."));
			return;
		}
		emit live_published();
	});
}

} // namespace smulti
