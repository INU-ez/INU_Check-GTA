static void windowLightingTest()
{
	if(gLampDffCache.empty()){ mvChkWrite("window test: no source model\n"); return; }
	GDff source; std::string err;
	if(!ParseDffGeoms(gLampDffCache.begin()->second, source, err)) return;
	GMat material; bool found = false;
	for(const GGeom &g : source.geoms) for(const GMat &m : g.mats) if(!m.tex.empty()){ material = m; found = true; break; }
	if(!found){ mvChkWrite("window test: no textured material\n"); return; }
	GGeom g; g.prelit = g.night = true; g.flags = 8; g.mats.push_back(material);
	for(int w = 0; w < 12; w++){
		float x = (float)(w % 4) * 3, z = (float)(w / 4) * 4;
		V3 p[4] = { {x,0,z}, {x+1,0,z}, {x+1,0,z+1}, {x,0,z+1} };
		int order[6] = {0,1,2,0,2,3};
		for(int k = 0; k < 6; k++){ g.pos.push_back(p[order[k]]); g.day.push_back(0xFF121314u + (uint32_t)(w * 3 + k)); g.nightCol.push_back(0xFF030405u); }
		for(int t = 0; t < 2; t++){ GTri tri = {{(uint32_t)(w*6+t*3), (uint32_t)(w*6+t*3+1), (uint32_t)(w*6+t*3+2)}, 0}; g.tri.push_back(tri); }
	}
	std::vector<uint8_t> geometry; WriteDffGeometry(g, source.version, geometry);
	auto chunk = [&](uint32_t id, const std::vector<uint8_t> &body){
		std::vector<uint8_t> out;
		for(uint32_t v : {id, (uint32_t)body.size(), (uint32_t)0x1803FFFFu}) for(int i=0;i<4;i++) out.push_back((uint8_t)(v >> (8*i)));
		out.insert(out.end(), body.begin(), body.end()); return out;
	};
	std::vector<uint8_t> list = chunk(1, {1,0,0,0}); list.insert(list.end(), geometry.begin(), geometry.end());
	std::vector<uint8_t> dff = chunk(16, chunk(26, list));
	BakeOptions o; o.windowsOnly = true; o.winTex = { lower(material.tex) }; o.sun = o.night = false;
	o.winLight = 0; o.rays = 8; o.winRandom = 1; o.winChance = 0.5f; o.winEmit = 0.8f;
	BakeScene scene; int failures = 0, lit = 0;
	BakeResult first, repeat, off, all, rows;
	if(!BakePrelit(dff, nullptr, scene, o, first, nullptr, nullptr)){ mvChkWrite(("window test: " + first.err + "\n").c_str()); return; }
	BakePrelit(dff, nullptr, scene, o, repeat, nullptr, nullptr);
	o.winChance = 0; BakePrelit(dff, nullptr, scene, o, off, nullptr, nullptr);
	o.winChance = 1; BakePrelit(dff, nullptr, scene, o, all, nullptr, nullptr);
	o.winChance = 0.5f; o.winRandom = 2; BakePrelit(dff, nullptr, scene, o, rows, nullptr, nullptr);
	if(first.geoms.empty() || repeat.geoms.empty() || off.geoms.empty() || all.geoms.empty() || rows.geoms.empty()){ mvChkWrite("window test: missing results\n"); return; }
	for(size_t i = 0; i < g.day.size(); i++){
		if(first.geoms[0].day[i] != g.day[i]) failures++;
		if(first.geoms[0].night[i] != repeat.geoms[0].night[i]) failures++;
		if(off.geoms[0].night[i] != g.nightCol[i]) failures++;
		if((all.geoms[0].night[i] & 0xFFFFFFu) == 0) failures++;
		if(first.geoms[0].night[i] != first.geoms[0].night[(i/6)*6]) failures++;
		if(rows.geoms[0].night[i] != rows.geoms[0].night[(i/24)*24]) failures++;
	}
	for(int w=0;w<12;w++) if(first.geoms[0].night[(size_t)w*6] != g.nightCol[(size_t)w*6]) lit++;
	if(lit == 0 || lit == 12) failures++;
	GGeom receiver = g; receiver.pos.push_back({0.5f,1.0f,0.5f}); receiver.pos.push_back({1.5f,1.0f,0.5f}); receiver.pos.push_back({0.5f,1.0f,1.5f});
	for(int k=0;k<3;k++){ receiver.day.push_back(0xFF121314u); receiver.nightCol.push_back(0xFF030405u); }
	GMat wall = material; wall.tex = "test_wall"; receiver.mats.push_back(wall);
	GTri rt={{72,73,74},1}; receiver.tri.push_back(rt);
	geometry.clear(); WriteDffGeometry(receiver, source.version, geometry);
	list=chunk(1,{1,0,0,0}); list.insert(list.end(),geometry.begin(),geometry.end()); dff=chunk(16,chunk(26,list));
	o.smoothIter=2; o.planarFit=true; o.winRandom=0; o.winLight=1; o.winRange=10; BakeResult spill; BakePrelit(dff,nullptr,scene,o,spill,nullptr,nullptr);
	if(spill.geoms.empty() || spill.winLamps==0) failures++;
	else { if(spill.geoms[0].night[72]==receiver.nightCol[72]) failures++; if(spill.geoms[0].day[72]!=receiver.day[72]) failures++; }
	BakeSpline sp; BakeNode a,b; b.p[0]=18; sp.pts={a,b}; sp.step=6;
	std::vector<BakeLight> lamps; BakeSplineLights(sp,lamps);
	if(lamps.size()!=4) failures++;
	sp.on=false; std::vector<BakeLight> disabled; BakeSplineLights(sp,disabled); if(!disabled.empty()) failures++;
	mvChkWrite(fmt("window/spline test: failures %d, lit islands %d/12, spline lamps %d\n", failures, lit, (int)lamps.size()).c_str());
}
