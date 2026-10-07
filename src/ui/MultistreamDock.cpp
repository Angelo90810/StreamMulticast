/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#include "MultistreamDock.hpp"
#include "HealthTab.hpp"
#include "ConfigTab.hpp"
#include "HubTab.hpp"

#include <QVBoxLayout>
#include <QTabWidget>
#include <QScrollArea>
#include <QSizePolicy>
#include <QFrame>

namespace smulti {

MultistreamDock::MultistreamDock(EndpointRegistry &registry,
                                 HealthSampler    &sampler,
                                 QWidget          *parent)
	: QWidget(parent)
	, m_registry(registry)
	, m_sampler(sampler)
{
	setup_ui();
}

void MultistreamDock::on_obs_frontend_ready()
{
	if (m_hub_tab)
		m_hub_tab->on_obs_frontend_ready();
}

void MultistreamDock::setup_ui()
{
	setObjectName("StreamMulticastDock");
	setMinimumSize(0, 0);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(0);

	m_tabs = new QTabWidget(this);
	m_tabs->setDocumentMode(false);
	m_tabs->setMinimumSize(0, 0);
	m_tabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

	auto *hub_scroll = new QScrollArea(m_tabs);
	hub_scroll->setWidgetResizable(true);
	hub_scroll->setFrameShape(QFrame::NoFrame);
	hub_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	hub_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	hub_scroll->setMinimumSize(0, 0);
	hub_scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

	m_hub_tab = new HubTab(m_registry, hub_scroll);
	m_hub_tab->setMinimumSize(0, 0);
	m_hub_tab->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	hub_scroll->setWidget(m_hub_tab);

	m_health_tab = new HealthTab(m_registry, m_sampler, m_tabs);
	m_health_tab->setMinimumSize(0, 0);
	m_health_tab->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

	m_config_tab = new ConfigTab(m_registry, m_tabs);
	m_config_tab->setMinimumSize(0, 0);
	m_config_tab->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

	m_tabs->addTab(hub_scroll, tr("Hub"));
	m_tabs->addTab(m_health_tab, tr("Health"));
	m_tabs->addTab(m_config_tab, tr("Configure"));

	/* Broadcast Hub is the primary v2 workflow. */
	m_tabs->setCurrentIndex(0);

	layout->addWidget(m_tabs);
	setLayout(layout);
}

} // namespace smulti
