/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is Netscape Communications
 * Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 *   Chris Waterson <waterson@netscape.com>
 */

#include "nsTemplateMatchSet.h"
#include "nsTemplateRule.h"

#ifdef DEBUG_waterson
#define NSTEMPLATEMATCHREFSET_METER // collect stats about match set sizes
#endif

//----------------------------------------------------------------------
//
// nsTemplateMatchSet
//

nsTemplateMatchSet::~nsTemplateMatchSet()
{
    while (mHead) {
        Element* doomed = mHead;
        mHead = mHead->mNext;
        doomed->mMatch->Release(mPool);
        delete doomed;
    }

    MOZ_COUNT_DTOR(nsTemplateMatchSet);
}

//----------------------------------------------------------------------
//
// nsTemplateMatchRefSet
//

PLDHashTableOps nsTemplateMatchRefSet::gOps = {
    PL_DHashAllocTable,
    PL_DHashFreeTable,
    PL_DHashGetKeyStub,
    HashEntry,
    MatchEntry,
    PL_DHashMoveEntryStub,
    PL_DHashClearEntryStub,
    PL_DHashFinalizeStub
};

PLDHashNumber PR_CALLBACK
nsTemplateMatchRefSet::HashEntry(PLDHashTable* aTable, const void* aKey)
{
    const nsTemplateMatch* match = NS_STATIC_CAST(const nsTemplateMatch*, aKey);
    return PLDHashNumber(Instantiation::Hash(&(match->mInstantiation)))
        ^ (PLDHashNumber(match->mRule) >> 2);
}

PRBool PR_CALLBACK
nsTemplateMatchRefSet::MatchEntry(PLDHashTable* aTable, const PLDHashEntryHdr* aHdr, const void* aKey)
{
    const Entry* entry = NS_REINTERPRET_CAST(const Entry*, aHdr);
    const nsTemplateMatch* left  = entry->mMatch;
    const nsTemplateMatch* right = NS_STATIC_CAST(const nsTemplateMatch*, aKey);
    return *left == *right;
}

#ifdef NSTEMPLATEMATCHREFSET_METER
#include "nsVoidArray.h"
static PRInt32 gCount;
static nsVoidArray gCountDistribution;
#endif

void
nsTemplateMatchRefSet::Init()
{
#ifdef NSTEMPLATEMATCHREFSET_METER
    ++gCount;
#endif
    mInlineMatches.mCount = 0;
}

void
nsTemplateMatchRefSet::Finish()
{
#ifdef NSTEMPLATEMATCHREFSET_METER
    {
        PRInt32 entries = mInlineMatches.mCount <= kMaxInlineMatches
            ? PRInt32(mInlineMatches.mCount)
            : mTable.entryCount;

        PRInt32 count = PRInt32(gCountDistribution[entries]);
        gCountDistribution.ReplaceElementAt((void*) ++count, entries);

        --gCount;
        if (gCount == 0) {
            printf("nsTemplateMatchRefSet distribution\n");
            for (PRInt32 i = gCountDistribution.Count() - 1; i >= 0; --i)
                printf("%d: %d\n", i, gCountDistribution[i]);
        }
    }
#endif

    if (mInlineMatches.mCount > kMaxInlineMatches)
        // It's a hashtable, so finish the table properly
        PL_DHashTableFinish(&mTable);

    mInlineMatches.mCount = 0;
}

void
nsTemplateMatchRefSet::CopyFrom(const nsTemplateMatchRefSet& aSet)
{
    ConstIterator last = aSet.Last();
    for (ConstIterator iter = aSet.First(); iter != last; ++iter)
        Add(iter.operator->());
}

PRBool
nsTemplateMatchRefSet::Empty() const
{
    PRUint32 count = mInlineMatches.mCount;
    if (count <= kMaxInlineMatches)
        return count == 0;

    return mTable.entryCount == 0;
}

PRBool
nsTemplateMatchRefSet::Contains(const nsTemplateMatch* aMatch) const
{
    PRUint32 count = mInlineMatches.mCount;
    if (count <= kMaxInlineMatches) {
        while (PRInt32(--count) >= 0) {
            if (*(mInlineMatches.mEntries[count]) == *aMatch)
                return PR_TRUE;
        }

        return PR_FALSE;
    }

    PLDHashEntryHdr* hdr =
        PL_DHashTableOperate(NS_CONST_CAST(PLDHashTable*, &mTable), aMatch,
                             PL_DHASH_LOOKUP);

    return PL_DHASH_ENTRY_IS_BUSY(hdr);
}

PRBool
nsTemplateMatchRefSet::Add(const nsTemplateMatch* aMatch)
{
    // Cast away const, we assume shared ownership.
    nsTemplateMatch* match = NS_CONST_CAST(nsTemplateMatch*, aMatch);

    PRUint32 count = mInlineMatches.mCount;
    if (count < kMaxInlineMatches) {
        // There's still room in the inline matches.

        // Check for duplicates
        for (PRInt32 i = PRInt32(count) - 1; i >= 0; --i) {
            if (*(mInlineMatches.mEntries[i]) == *aMatch)
                return PR_FALSE;
        }

        // Nope. Add it!
        mInlineMatches.mEntries[count] = match;

        ++mInlineMatches.mCount;
        return PR_TRUE;
    }

    if (count == kMaxInlineMatches) {
        // No room left in inline matches. Fault, and convert to
        // hashtable.
        nsTemplateMatch* temp[kMaxInlineMatches];
        PRInt32 i;

        // Copy pointers to a safe place
        for (i = count - 1; i >= 0; --i)
            temp[i] = mInlineMatches.mEntries[i];

        // Clobber the union; we'll treat it as a hashtable now.
        PL_DHashTableInit(&mTable, &gOps, nsnull, sizeof(Entry), PL_DHASH_MIN_SIZE);

        // Now that we've table-ized this thing, mCount better be a
        // big freaking number, since it's sharing space with a
        // pointer to the PLDHashTable's ops.
        NS_ASSERTION(mInlineMatches.mCount > kMaxInlineMatches,
                     "wow, I thought it'd be bigger than _that_");

        // Copy the pointers into the hashtable.
        for (i = count - 1; i >= 0; --i)
            AddToTable(temp[i]);
    }

    return AddToTable(match);
}

PRBool
nsTemplateMatchRefSet::AddToTable(nsTemplateMatch* aMatch)
{
    PLDHashEntryHdr* hdr =
        PL_DHashTableOperate(&mTable, aMatch, PL_DHASH_ADD);

    if (hdr) {
        Entry* entry = NS_REINTERPRET_CAST(Entry*, hdr);

        if (! entry->mMatch) {
            entry->mMatch = aMatch;
            return PR_TRUE;
        }
    }

    return PR_FALSE;
}

PRBool
nsTemplateMatchRefSet::Remove(const nsTemplateMatch* aMatch)
{
    PRBool found = PR_FALSE;

    PRUint32 count = mInlineMatches.mCount;
    if (count <= kMaxInlineMatches) {
        nsTemplateMatch** last;

        for (PRUint32 i = 0; i < count; ++i) {
            nsTemplateMatch** entry = &mInlineMatches.mEntries[i];
            nsTemplateMatch* match = *entry;

            if (*match == *aMatch) {
                found = PR_TRUE;
            }
            else if (found)
                *last = match;

            last = entry;
        }

        if (found)
            --mInlineMatches.mCount;
    }
    else {
        PLDHashEntryHdr* hdr =
            PL_DHashTableOperate(&mTable, aMatch, PL_DHASH_LOOKUP);

        found = PL_DHASH_ENTRY_IS_BUSY(hdr);

        if (found)
            // Remove the match. N.B., we never demote back to a list.
            PL_DHashTableOperate(&mTable, aMatch, PL_DHASH_REMOVE);
    }

    return found;
}

//----------------------------------------------------------------------
//
// ConstIterator
//

#define ENTRY_IS_LIVE(entry) (PL_DHASH_ENTRY_IS_BUSY(&((entry)->mHdr)) && (entry)->mMatch)

nsTemplateMatchRefSet::ConstIterator
nsTemplateMatchRefSet::First() const
{
    if (mInlineMatches.mCount <= kMaxInlineMatches)
        // XXX C-style cast to avoid gcc-2.7.2.3. bustage
        return ConstIterator(this, (nsTemplateMatch**) mInlineMatches.mEntries);

    Entry* entry = NS_REINTERPRET_CAST(Entry*, mTable.entryStore);
    Entry* limit = entry + PR_BIT(mTable.sizeLog2);
    for ( ; entry < limit; ++entry) {
        if (ENTRY_IS_LIVE(entry))
            break;
    }

    return ConstIterator(this, entry);
}

nsTemplateMatchRefSet::ConstIterator
nsTemplateMatchRefSet::Last() const
{
    PRUint32 count = mInlineMatches.mCount;
    if (count <= kMaxInlineMatches) {
        // XXX C-style cast to avoid gcc-2.7.2.3 bustage
        nsTemplateMatch** first = (nsTemplateMatch**) mInlineMatches.mEntries;
        nsTemplateMatch** limit = first + count;
        return ConstIterator(this, limit);
    }

    Entry* limit = NS_REINTERPRET_CAST(Entry*, mTable.entryStore);
    limit += PR_BIT(mTable.sizeLog2);
    return ConstIterator(this, limit);
}

void
nsTemplateMatchRefSet::ConstIterator::Next()
{
    if (mSet->mInlineMatches.mCount <= kMaxInlineMatches)
        ++mInlineEntry;
    else {
        const PLDHashTable& table = mSet->mTable;
        Entry* limit = NS_REINTERPRET_CAST(Entry*, table.entryStore);
        limit += PR_BIT(table.sizeLog2);
        while (++mTableEntry < limit) {
            if (ENTRY_IS_LIVE(mTableEntry))
                break;
        }
    }
}

void
nsTemplateMatchRefSet::ConstIterator::Prev()
{
    if (mSet->mInlineMatches.mCount <= kMaxInlineMatches)
        --mInlineEntry;
    else {
        const PLDHashTable& table = mSet->mTable;
        Entry* limit = NS_REINTERPRET_CAST(Entry*, table.entryStore);
        while (--mTableEntry > limit) {
            if (ENTRY_IS_LIVE(mTableEntry))
                break;
        }
    }
}

PRBool
nsTemplateMatchRefSet::ConstIterator::operator==(const ConstIterator& aConstIterator) const
{
    if (mSet != aConstIterator.mSet)
        return PR_FALSE;

    PRUint32 count = mSet->mInlineMatches.mCount;
    if (count <= kMaxInlineMatches)
        return mInlineEntry == aConstIterator.mInlineEntry;

    return mTableEntry == aConstIterator.mTableEntry;
}
