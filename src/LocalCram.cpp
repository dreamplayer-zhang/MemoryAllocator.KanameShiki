


namespace KanameShiki {



alignas(csCacheLine) std::atomic_uint64_t gnLocalCram(0);



uint64_t NumLocalCram() noexcept
{
	return gnLocalCram.load(std::memory_order_acquire);
}



LocalCram::~LocalCram() noexcept
{
	assert(mnCache.load(std::memory_order_acquire) == 0);
	
	#if KANAMESHIKI_DEBUG_LEVEL//[
	gnLocalCram.fetch_sub(1, std::memory_order_acq_rel);
	#endif//]
}



LocalCram::LocalCram(LocalCntx* pOwner, uint16_t b)
:mId(std::this_thread::get_id())
,mpOwner(pOwner)
,mb(b)
,mRealm(Tag::Realm(bit(b)))
,mvParcel(to_t(this+1))
,meParcel(mvParcel + (bit(b) * cnCramParcel))
,maCache{}
,mbCache(true)
,mnCache(0)
{
	assert(!(to_t(this) & cmCacheLine));
	
	#if KANAMESHIKI_DEBUG_LEVEL//[
	gnLocalCram.fetch_add(1, std::memory_order_acq_rel);
	#endif//]
}



std::size_t LocalCram::Size(Parcel* pParcel) const noexcept
{
	Auto pTag = Tag::CastTag(pParcel);
	Auto RealmT = pTag->Realm();
	return Tag::Size(RealmT + mRealm);
}



void LocalCram::Free(Parcel* pParcel) noexcept
{
	if (mbCache.load(std::memory_order_acquire)){
		Auto pTag = Tag::CastTag(pParcel);
		Auto RealmT = pTag->Realm();
		assert(RealmT < numof(maCache));
		auto& rCache = maCache[RealmT];
		
		if (mId == std::this_thread::get_id()){
			rCache.ListST(pParcel); return;
		} else {
			if (rCache.ListMT(pParcel)) return;
		}
	}
	if (DecCache(1) == 0) Delete();
}



void* LocalCram::Alloc(std::size_t s) noexcept
{
	Auto RealmS = Tag::Realm(s);
	Auto RealmT = RealmS - mRealm;
	assert(RealmT < numof(maCache));
	auto& rCache = maCache[RealmT];
	
	auto& rListST = rCache.mListST;
	Auto pParcel = rListST.p;
	if (pParcel){
		rListST.p = pParcel->Alloc(this);
		return pParcel->CastData();
	} else {
		Auto oRevolver = rCache.mRevolverAlloc.o & rCache.RevolverMask();
		pParcel = rCache.maListMT[oRevolver].p.exchange(nullptr, std::memory_order_acq_rel);
		if (pParcel){
			rCache.mRevolverAlloc.o = ++oRevolver;
			pParcel->Alloc(this);
			return pParcel->CastData();
		} else {
			Auto vParcel = mvParcel;
			{	// 
				Auto sTagT = Tag::SizeofT();
				Auto sParcelT = Parcel::SizeofT();
				vParcel = align_t(vParcel + sTagT + sParcelT, csAlign) - sParcelT;
				pParcel = reinterpret_cast<Parcel*>(vParcel);
			}
			Auto p = pParcel->CastData();
			
			vParcel = to_t(p) + Tag::Size(RealmS);
			if (vParcel <= meParcel){
				mvParcel = vParcel;
				
				pParcel->Alloc(this);
				{	// 
					Auto pTag = Tag::CastTag(pParcel);
					new(pTag) Tag(RealmT);
				}
				
				mnCache.fetch_add(1, std::memory_order_acq_rel);
				assert(mnCache.load(std::memory_order_acquire) <= cnCramParcel);
				return p;
			} else {
				assert(mnCache.load(std::memory_order_acquire));
				Clearance();
				return mpOwner->NewCram(mb, s);
			}
		}
	}
}



uint16_t LocalCram::Clearance() noexcept
{
	mbCache.store(false, std::memory_order_release);
	
	uint16_t nCache = 0;
	for (auto& rCache : maCache) nCache += rCache.Clearance();
	return DecCache(nCache);
}



void LocalCram::Delete() noexcept
{
	Auto bSelf = (mId == std::this_thread::get_id());
	
	this->~LocalCram();
	
	if (bSelf){
		LocalCntxPtr()->ReserverFree(this);
	} else {
		GlobalCntxPtr()->ReserverFree(this);
	}
}



void* LocalCram::operator new(std::size_t sThis, uint16_t b, const std::nothrow_t&) noexcept
{
	Auto sBudget = sThis + (bit(b) * cnCramParcel);
	return LocalCntxPtr()->ReserverAlloc(sBudget);
}



uint16_t LocalCram::DecCache(uint16_t nCache) noexcept
{
	return mnCache.fetch_sub(nCache, std::memory_order_acq_rel) - nCache;
}



}
