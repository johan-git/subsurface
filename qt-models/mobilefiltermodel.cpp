// SPDX-License-Identifier: GPL-2.0
#include "qt-models/mobilefiltermodel.h"
#include "qt-models/mobilelistmodel.h"
#include "core/divelist.h"

MobileFilterModel *MobileFilterModel::instance()
{
	static MobileFilterModel self;
	return &self;
}

MobileFilterModel::MobileFilterModel()
{
	setFilterKeyColumn(-1); // filter all columns
	setFilterRole(DiveTripModelBase::SHOWN_ROLE); // Let the proxy-model known that is has to react to change events involving SHOWN_ROLE

	MobileListModel *m = MobileModels::instance()->listModel();
	setSourceModel(m);
}

// It is annoying that we can't simply map a row. We have to map a full index.
int MobileFilterModel::mapRowToSource(int row)
{
	QModelIndex local = index(row, 0, QModelIndex());
	return mapToSource(local).row();
}

int MobileFilterModel::shown()
{
	return shown_dives;
}

void MobileFilterModel::toggle(int row)
{
	MobileModels::instance()->listModel()->toggle(mapRowToSource(row));
}

QVariantMap MobileFilterModel::get(int row) const
{
	QHashIterator <int, QByteArray> it(roleNames());
	QVariantMap res;
	while (it.hasNext()) {
		it.next();
		res[it.value()] = index(row,0 ).data(it.key());
	}
	return res;
}

bool MobileFilterModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
{
	QAbstractItemModel *m = sourceModel();
	QModelIndex index0 = m->index(source_row, 0, source_parent);
	return m->data(index0, DiveTripModelBase::SHOWN_ROLE).value<bool>();
}
