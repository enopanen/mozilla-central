/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1999 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

#include "msgCore.h"
#include "nsMsgThread.h"
#include "nsMsgDatabase.h"
#include "nsCOMPtr.h"

NS_IMPL_ISUPPORTS(nsMsgThread, nsMsgThread::GetIID())

nsMsgThread::nsMsgThread()
{
	Init();
}
nsMsgThread::nsMsgThread(nsMsgDatabase *db, nsIMdbTable *table)
{
	Init();
	m_mdbTable = table;
	m_mdbDB = db;
	if (db)
		db->AddRef();

	if (table && db)
	{
		table->GetMetaRow(db->GetEnv(), nil, nil, &m_metaRow);
		InitCachedValues();
	}
}

void nsMsgThread::Init()
{
	m_threadKey = nsMsgKey_None; 
	m_numChildren = 0;		
	m_numUnreadChildren = 0;	
	m_flags = 0;
	m_mdbTable = nsnull;
	m_mdbDB = nsnull;
	m_metaRow = nsnull;
	m_cachedValuesInitialized = PR_FALSE;
	NS_INIT_REFCNT();
}


nsMsgThread::~nsMsgThread()
{
	if (m_mdbTable)
		m_mdbTable->Release();
	if (m_mdbDB)
		m_mdbDB->Release();
	if (m_metaRow)
		m_metaRow->Release();
}

nsresult nsMsgThread::InitCachedValues()
{
	nsresult err = NS_OK;

	if (!m_mdbDB || !m_metaRow)
	    return NS_ERROR_NULL_POINTER;

	if (!m_cachedValuesInitialized)
	{
		err = m_mdbDB->RowCellColumnToUInt32(m_metaRow, m_mdbDB->m_threadFlagsColumnToken, &m_flags);
	    err = m_mdbDB->RowCellColumnToUInt32(m_metaRow, m_mdbDB->m_threadChildrenColumnToken, &m_numChildren);
		err = m_mdbDB->RowCellColumnToUInt32(m_metaRow, m_mdbDB->m_threadIdColumnToken, &m_threadKey);
	    err = m_mdbDB->RowCellColumnToUInt32(m_metaRow, m_mdbDB->m_threadUnreadChildrenColumnToken, &m_numUnreadChildren);

		
		if (NS_SUCCEEDED(err))
			m_cachedValuesInitialized = PR_TRUE;
	}
	return err;
}

NS_IMETHODIMP		nsMsgThread::SetThreadKey(nsMsgKey threadKey)
{
	nsresult ret = NS_OK;

	m_threadKey = threadKey;
	ret = m_mdbDB->UInt32ToRowCellColumn(m_metaRow, m_mdbDB->m_threadIdColumnToken, threadKey);
	// gotta set column in meta row here.
	return ret;
}

NS_IMETHODIMP	nsMsgThread::GetThreadKey(nsMsgKey *result)
{
	if (result)
	{
		nsresult res = m_mdbDB->RowCellColumnToUInt32(m_metaRow, m_mdbDB->m_threadIdColumnToken, &m_threadKey);
		*result = m_threadKey;
		return res;
	}
	else
		return NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP nsMsgThread::GetFlags(PRUint32 *result)
{
	if (result)
	{
		nsresult res = m_mdbDB->RowCellColumnToUInt32(m_metaRow, m_mdbDB->m_threadFlagsColumnToken, &m_flags);
		*result = m_flags;
		return res;
	}
	else
		return NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP nsMsgThread::SetFlags(PRUint32 flags)
{
	nsresult ret = NS_OK;

	m_flags = flags;
	ret = m_mdbDB->UInt32ToRowCellColumn(m_metaRow, m_mdbDB->m_threadFlagsColumnToken, m_flags);
	return ret;
}


NS_IMETHODIMP nsMsgThread::GetNumChildren(PRUint32 *result)
{
	if (result)
	{
		*result = m_numChildren;
		return NS_OK;
	}
	else
		return NS_ERROR_NULL_POINTER;
}


NS_IMETHODIMP nsMsgThread::GetNumUnreadChildren (PRUint32 *result)
{
	if (result)
	{
		*result = m_numUnreadChildren;
		return NS_OK;
	}
	else
		return NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP nsMsgThread::AddChild(nsIMsgDBHdr *child, PRBool threadInThread)
{
	nsresult ret = NS_OK;
    nsMsgHdr* hdr = NS_STATIC_CAST(nsMsgHdr*, child);          // closed system, cast ok

	nsIMdbRow *hdrRow = hdr->GetMDBRow();
	if (m_mdbTable)
	{
		m_mdbTable->AddRow(m_mdbDB->GetEnv(), hdrRow);
		ChangeChildCount(1);
	}

	return ret;
}


NS_IMETHODIMP nsMsgThread::GetChildAt(PRInt32 aIndex, nsIMsgDBHdr **result)
{
	nsresult ret = NS_OK;

	return ret;
}


NS_IMETHODIMP nsMsgThread::GetChild(nsMsgKey msgKey, nsIMsgDBHdr **result)
{
	nsresult ret = NS_OK;
	mdb_bool	hasOid;
	mdbOid		rowObjectId;


	if (!result || !m_mdbTable)
		return NS_ERROR_NULL_POINTER;

	*result = NULL;
	rowObjectId.mOid_Id = msgKey;
	rowObjectId.mOid_Scope = m_mdbDB->m_hdrRowScopeToken;
	ret = m_mdbTable->HasOid(m_mdbDB->GetEnv(), &rowObjectId, &hasOid);
	if (NS_SUCCEEDED(ret) && hasOid && m_mdbDB && m_mdbDB->m_mdbStore)
	{
		nsIMdbRow *hdrRow = nsnull;
		ret = m_mdbDB->m_mdbStore->GetRow(m_mdbDB->GetEnv(), &rowObjectId,  &hdrRow);
		if (ret == NS_OK && hdrRow && m_mdbDB)
		{
			ret = m_mdbDB->CreateMsgHdr(hdrRow,  msgKey, result);
		}
	}
	return ret;
}


NS_IMETHODIMP nsMsgThread::GetChildHdrAt(PRInt32 aIndex, nsIMsgDBHdr **result)
{
	nsresult ret = NS_OK;
	nsIMdbRow* resultRow;
	mdb_pos pos = aIndex - 1;

	if (!result)
		return NS_ERROR_NULL_POINTER;

	*result = nsnull;
	// mork doesn't seem to handle this correctly, so deal with going off
	// the end here.
	if (aIndex > m_numChildren)
		return NS_OK;

	nsIMdbTableRowCursor *rowCursor;
	ret = m_mdbTable->GetTableRowCursor(m_mdbDB->GetEnv(), pos, &rowCursor);

	ret = rowCursor->NextRow(m_mdbDB->GetEnv(), &resultRow, &pos);
	NS_RELEASE(rowCursor);
	if (NS_FAILED(ret) || !resultRow) 
		return ret;

	//Get key from row
	mdbOid outOid;
	nsMsgKey key;
	if (resultRow->GetOid(m_mdbDB->GetEnv(), &outOid) == NS_OK)
		key = outOid.mOid_Id;

	ret = m_mdbDB->CreateMsgHdr(resultRow, key, result);
	if (NS_FAILED(ret)) 
		return ret;
	return ret;
}


NS_IMETHODIMP nsMsgThread::RemoveChildAt(PRInt32 aIndex)
{
	nsresult ret = NS_OK;

	return ret;
}


NS_IMETHODIMP nsMsgThread::RemoveChild(nsMsgKey msgKey)
{
	nsresult ret = NS_OK;
	mdbOid		rowObjectId;
	rowObjectId.mOid_Id = msgKey;
	rowObjectId.mOid_Scope = m_mdbDB->m_hdrRowScopeToken;
	ret = m_mdbTable->CutOid(m_mdbDB->GetEnv(), &rowObjectId);
	return ret;
}


NS_IMETHODIMP nsMsgThread::MarkChildRead(PRBool bRead)
{
	nsresult ret = NS_OK;

	return ret;
}

PRBool nsMsgThread::TryReferenceThreading(nsIMsgDBHdr *newHeader)
{
	// start at end of references to find immediate parent in thread
	PRBool addedChild = PR_FALSE;
	PRBool done = PR_FALSE;
#if 0
	for (int32 refIndex = newHeader->GetNumReferences() - 1; !done && refIndex >= 0; refIndex--)
	{
		nsCOMPtr <nsIMsgDBHdr>  refHdr;
		refHdr = messageDB->GetNeoMessageHdrForHashMessageID(newHeader->GetReferenceId(refIndex));
		if (refHdr)
		{
			// position iterator at child pos.
			childIterator.reset();
			while (TRUE)
			{
				MessageKey curMsgId = childIterator.currentID();
				if (curMsgId == 0)
					break;
				if (curMsgId == refHdr->fID)
					break;
				childIterator.nextID();
			}
			// new header is a reply to header at child index. Its level
			// is the childIndex header + 1. We need
			// to put it in the m_children array with the other children of
			// the document we are a reply to, in date order.
			newHeader->SetLevel(refHdr->GetLevel() + 1);
			while (childIterator.currentID() != 0 && !done)
			{
				MessageKey msgId = childIterator.currentID();
				if (msgId != 0)
				{
					nsIMsgDBHdr *childHdr = (nsIMsgDBHdr *) childIterator.currentObject();
					if (childHdr != NULL)
					{
						// If the current header is at a equal or higher level, we've reached the end
						// of the current level, so insert here.
						// If the current header is at the desired level, and is later than
						// the new header, put the new header before the current header.
						// Or if we're at the end, insert here.
						if ((childHdr != refHdr && childHdr->GetLevel() <= refHdr->GetLevel())
							|| (childHdr->GetLevel() == newHeader->GetLevel() 
								&& newHeader->GetDate() < childHdr->GetDate())
							|| childIterator.nextID() == 0) // evil side effect - advance cursor
						{
							messageDB->AddNeoHdr(newHeader);
							// it seems if we get to the end, addHere sticks object at
							// beginning of parts list. That's not what we want.
							if (childIterator.currentID() != 0)
								childIterator.addHere(newHeader, TRUE /* insert before cursor */);
							else
								childIterator.addObject(newHeader);
							newHeader->unrefer();	// adding a newHeader to a part mgr adds a reference
							done = PR_TRUE;
							addedChild = PR_TRUE;
						}
					}
				}
			}
			refHdr->unrefer();
			break;
		}
	}
#endif
	return addedChild;
}


class nsMsgThreadEnumerator : public nsIEnumerator {
public:
    NS_DECL_ISUPPORTS

    // nsIEnumerator methods:
    NS_IMETHOD First(void);
    NS_IMETHOD Next(void);
    NS_IMETHOD CurrentItem(nsISupports **aItem);
    NS_IMETHOD IsDone(void);

    // nsMsgThreadEnumerator methods:
    typedef nsresult (*nsMsgThreadEnumeratorFilter)(nsIMsgDBHdr* hdr, void* closure);

    nsMsgThreadEnumerator(nsMsgThread *thread, nsMsgKey startKey,
                      nsMsgThreadEnumeratorFilter filter, void* closure);
    virtual ~nsMsgThreadEnumerator();

protected:
	nsIMdbTableRowCursor*       mRowCursor;
    nsIMsgDBHdr*                 mResultHdr;
	nsMsgThread*				mThread;
	nsMsgKey					mCurKey;
	PRInt32						mChildIndex;
    PRBool                      mDone;
    nsMsgThreadEnumeratorFilter     mFilter;
    void*                       mClosure;
};

nsMsgThreadEnumerator::nsMsgThreadEnumerator(nsMsgThread *thread, nsMsgKey startKey,
                                     nsMsgThreadEnumeratorFilter filter, void* closure)
    : mRowCursor(nsnull), mResultHdr(nsnull), mDone(PR_FALSE),
      mFilter(filter), mClosure(closure)
{
    NS_INIT_REFCNT();
	mCurKey = startKey;
	mChildIndex = 1;
	mThread = thread;
	if (mCurKey != nsMsgKey_None)
	{
		nsMsgKey msgKey = nsMsgKey_None;
		nsresult rv = mThread->GetChildHdrAt(0, &mResultHdr);
		if (NS_SUCCEEDED(rv) && mResultHdr)
		{
			// we're only doing one level of threading, so check if caller is
			// asking for children of the first message in the thread or not.
			// if not, we will tell him there are no children.
			mResultHdr->GetMessageKey(&msgKey);
			if (msgKey != mCurKey)
				mDone = PR_TRUE;

			NS_RELEASE(mResultHdr);
			mResultHdr = nsnull;

		}
		mChildIndex = 1;
	}
    NS_ADDREF(thread);
}

nsMsgThreadEnumerator::~nsMsgThreadEnumerator()
{
    NS_RELEASE(mThread);
}

NS_IMPL_ISUPPORTS(nsMsgThreadEnumerator, nsIEnumerator::GetIID())

NS_IMETHODIMP nsMsgThreadEnumerator::First(void)
{
	//nsresult rv = NS_OK;

	if (!mThread)
		return NS_ERROR_NULL_POINTER;
		
    return Next();
}

NS_IMETHODIMP nsMsgThreadEnumerator::Next(void)
{
	nsresult rv;
	if (mCurKey == nsMsgKey_None)
	{
		rv = mThread->GetChildHdrAt(0, &mResultHdr);
		mChildIndex = 1;
	}
	else if (!mDone)
	{
		rv  = mThread->GetChildHdrAt(mChildIndex++, &mResultHdr);
	}
	if (!mResultHdr) 
	{
        mDone = PR_TRUE;
		return NS_ERROR_FAILURE;
    }
    if (NS_FAILED(rv)) 
	{
        mDone = PR_TRUE;
        return rv;
    }

	return rv;
}

NS_IMETHODIMP nsMsgThreadEnumerator::CurrentItem(nsISupports **aItem)
{
    if (mResultHdr) {
        *aItem = mResultHdr;
        NS_ADDREF(mResultHdr);
        return NS_OK;
    }
    return NS_ERROR_FAILURE;
}

NS_IMETHODIMP nsMsgThreadEnumerator::IsDone(void)
{
    return mDone ? NS_OK : NS_COMFALSE;
}


NS_IMETHODIMP nsMsgThread::EnumerateMessages(nsMsgKey parentKey, nsIEnumerator* *result)
{
	nsresult ret = NS_OK;
	nsMsgThreadEnumerator* e = new nsMsgThreadEnumerator(this, parentKey, nsnull, nsnull);
    if (e == nsnull)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(e);
    *result = e;
    return NS_OK;

	return ret;
}

nsresult nsMsgThread::ChangeChildCount(PRInt32 delta)
{
	nsresult ret = NS_OK;
	PRUint32 childCount = 0;
	m_mdbDB->RowCellColumnToUInt32(m_metaRow, m_mdbDB->m_threadChildrenColumnToken, childCount);
	childCount += delta;

	ret = m_mdbDB->UInt32ToRowCellColumn(m_metaRow, m_mdbDB->m_threadChildrenColumnToken, childCount);
	return ret;
}

nsresult nsMsgThread::ChangeUnreadChildCount(PRInt32 delta)
{
	nsresult ret = NS_OK;
	PRUint32 childCount = 0;
	m_mdbDB->RowCellColumnToUInt32(m_metaRow, m_mdbDB->m_threadUnreadChildrenColumnToken, childCount);
	childCount += delta;

	ret = m_mdbDB->UInt32ToRowCellColumn(m_metaRow, m_mdbDB->m_threadUnreadChildrenColumnToken, childCount);
	return ret;
}
