/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#include "MultistreamDock.hpp"
#include "HealthTab.hpp"
#include "ConfigTab.hpp"

#include <QVBoxLayout>
#include <QTabWidget>
#include <QSizePolicy>

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

void MultistreamDock::setup_ui()
{
	setObjectName("StreamMulticastDock");
	setMinimumSize(0, 0);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

	auto *layout = new QVBoxLayout(this);
	layout->setSizeConstraint(QLayout::SetNoConstraint);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(0);

	m_tabs = new QTabWidget(this);
	m_tabs->setDocumentMode(false);
	m_tabs->setMinimumSize(0, 0);
	m_tabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

	m_health_tab = new HealthTab(m_registry, m_sampler, m_tabs);
	m_health_tab->setMinimumSize(0, 0);
	m_health_tab->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

	m_config_tab = new ConfigTab(m_registry, m_tabs);
	m_config_tab->setMinimumSize(0, 0);
	m_config_tab->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

	m_tabs->addTab(m_health_tab, tr("Health"));
	m_tabs->addTab(m_config_tab, tr("Configure"));
	m_tabs->setCurrentIndex(0);

	layout->addWidget(m_tabs);
	setLayout(layout);
}

} // namespace smulti
