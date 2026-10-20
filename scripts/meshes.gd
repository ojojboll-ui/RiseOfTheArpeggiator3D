class_name Meshes
## The game's lowpoly models, built from polygons in code.
##
## Every face is flat-shaded and carries barycentric coordinates (UV and
## UV2.x) for neon_mesh.gdshader's outlines. A polygon is cut into a fan of
## triangles, and the cuts are hidden by lifting their barycentric component
## out of reach, so only the polygon's own edges glow — a square face shows
## four edges, not five. Pass `diagonals = true` to show them (the splitter's
## crossed faces).
##
## Enemies are built at radius 1 and scaled by the world; local +x is
## forward, +y up. The ship is built in metres.


## Faces are PackedVector3Array polygons, convex and planar. Their winding is
## sorted out here: each face is turned to face away from the shape's centre.
static func solid(faces: Array, diagonals := false) -> ArrayMesh:
	var centre := Vector3.ZERO
	var count := 0
	for f in faces:
		for p in f:
			centre += p
			count += 1
	centre /= max(count, 1)

	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var uv := PackedVector2Array()
	var uv2 := PackedVector2Array()

	for f in faces:
		var poly: PackedVector3Array = f
		var n := (poly[1] - poly[0]).cross(poly[2] - poly[0]).normalized()
		var mid := Vector3.ZERO
		for p in poly:
			mid += p
		mid /= poly.size()
		if n.dot(mid - centre) < 0.0:
			poly.reverse()
			n = -n
		var last := poly.size() - 1
		for i in range(1, last):
			# Fan triangle (0, i, i+1): component 2 is the edge 0-i, 0 the edge
			# i-(i+1), 1 the edge (i+1)-0. Only the outer edges glow.
			var hide := Vector3.ZERO
			if not diagonals:
				if i != 1:
					hide.z = 1.0
				if i + 1 != last:
					hide.y = 1.0
			var tri := [poly[0], poly[i], poly[i + 1]]
			var bary := [Vector3(1, 0, 0) + hide, Vector3(0, 1, 0) + hide, Vector3(0, 0, 1) + hide]
			# Godot's front faces wind clockwise seen from outside.
			for k in [0, 2, 1]:
				verts.push_back(tri[k])
				normals.push_back(n)
				uv.push_back(Vector2(bary[k].x, bary[k].y))
				uv2.push_back(Vector2(bary[k].z, 0.0))

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_TEX_UV] = uv
	arrays[Mesh.ARRAY_TEX_UV2] = uv2
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


## A prism: a polygon in the xz plane, extruded from y0 to y1.
static func prism_faces(outline: PackedVector2Array, y0: float, y1: float) -> Array:
	var faces := []
	var top := PackedVector3Array()
	var bottom := PackedVector3Array()
	for p in outline:
		top.push_back(Vector3(p.x, y1, p.y))
		bottom.push_back(Vector3(p.x, y0, p.y))
	faces.push_back(top)
	faces.push_back(bottom)
	var n := outline.size()
	for i in n:
		var a := outline[i]
		var b := outline[(i + 1) % n]
		faces.push_back(PackedVector3Array([Vector3(a.x, y0, a.y), Vector3(b.x, y0, b.y), Vector3(b.x, y1, b.y), Vector3(a.x, y1, a.y)]))
	return faces


static func tri(a: Vector3, b: Vector3, c: Vector3) -> PackedVector3Array:
	return PackedVector3Array([a, b, c])


static func quad(a: Vector3, b: Vector3, c: Vector3, d: Vector3) -> PackedVector3Array:
	return PackedVector3Array([a, b, c, d])


## Pinwheel: four bent blades round a hub, each its own little prism.
static func wanderer() -> ArrayMesh:
	var faces := []
	for i in 4:
		var a := PI * 0.5 * i
		var blade := PackedVector2Array([
			Vector2(0.0, 0.0),
			Vector2(cos(a), sin(a)) * 1.0,
			Vector2(cos(a + 0.9), sin(a + 0.9)) * 0.75])
		faces.append_array(prism_faces(blade, -0.14, 0.14))
	return _merge(faces)


## Diamond: a tall octahedron standing on its point.
static func seeker() -> ArrayMesh:
	var top := Vector3(0, 1.3, 0)
	var bottom := Vector3(0, -0.8, 0)
	var ring := [Vector3(0.8, 0, 0), Vector3(0, 0, 0.8), Vector3(-0.8, 0, 0), Vector3(0, 0, -0.8)]
	var faces := []
	for i in 4:
		faces.push_back(tri(top, ring[i], ring[(i + 1) % 4]))
		faces.push_back(tri(bottom, ring[(i + 1) % 4], ring[i]))
	return solid(faces)


## Dodger: a square frame, turned 45 degrees.
static func dodger() -> ArrayMesh:
	var outer := 1.2
	var inner := 0.62
	var faces := []
	for i in 4:
		var a0 := PI * 0.25 + PI * 0.5 * i
		var a1 := a0 + PI * 0.5
		var o0 := Vector2(cos(a0), sin(a0)) * outer
		var o1 := Vector2(cos(a1), sin(a1)) * outer
		var i0 := Vector2(cos(a0), sin(a0)) * inner
		var i1 := Vector2(cos(a1), sin(a1)) * inner
		faces.append_array(prism_faces(PackedVector2Array([o0, o1, i1, i0]), -0.22, 0.22))
	return _merge(faces)


## Splitter: a cube with its faces crossed, the 2D square with an X.
static func splitter() -> ArrayMesh:
	var s := 0.9
	var outline := PackedVector2Array([Vector2(-s, -s), Vector2(s, -s), Vector2(s, s), Vector2(-s, s)])
	return solid(prism_faces(outline, -s, s), true)


## Shard: a tetrahedron.
static func shard() -> ArrayMesh:
	return solid(_tetra(1.15))


## Black hole: an icosahedron.
static func well() -> ArrayMesh:
	var t := (1.0 + sqrt(5.0)) / 2.0
	var v := [Vector3(-1, t, 0), Vector3(1, t, 0), Vector3(-1, -t, 0), Vector3(1, -t, 0),
		Vector3(0, -1, t), Vector3(0, 1, t), Vector3(0, -1, -t), Vector3(0, 1, -t),
		Vector3(t, 0, -1), Vector3(t, 0, 1), Vector3(-t, 0, -1), Vector3(-t, 0, 1)]
	for i in v.size():
		v[i] = v[i].normalized()
	var idx := [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2],
		[10, 7, 6], [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11],
		[6, 2, 10], [8, 6, 7], [9, 8, 1]]
	var faces := []
	for f in idx:
		faces.push_back(tri(v[f[0]], v[f[1]], v[f[2]]))
	return solid(faces)


## Power-up: a tetrahedron, smaller.
static func pickup() -> ArrayMesh:
	return solid(_tetra(1.0))


## The ship: a dart with a raised spine and a notch at the back, in metres,
## drawn 1.4 times the 2D ship's size like the enemies.
static func ship(s := 1.4) -> ArrayMesh:
	var nose := Vector3(0.26, 0.0, 0.0) * s
	var left := Vector3(-0.15, 0.0, 0.17) * s
	var right := Vector3(-0.15, 0.0, -0.17) * s
	var notch := Vector3(-0.07, 0.0, 0.0) * s
	var top := Vector3(-0.02, 0.075, 0.0) * s
	var under := Vector3(-0.02, -0.035, 0.0) * s
	var faces := [
		tri(nose, top, left), tri(nose, right, top),
		tri(left, top, notch), tri(notch, top, right),
		tri(nose, left, under), tri(nose, under, right),
		tri(left, notch, under), tri(notch, right, under)]
	return solid(faces)


## A zone's tower, in metres: a six-sided column narrowing upward and a
## crystal floating over it. `radius` is the column's foot.
static func tower(radius: float, height: float) -> ArrayMesh:
	var faces := []
	var top_r := radius * 0.55
	var top_y := height * 0.78
	var bottom := PackedVector3Array()
	var top := PackedVector3Array()
	for i in 6:
		var a := TAU * i / 6.0
		bottom.push_back(Vector3(cos(a) * radius, -0.6, sin(a) * radius))
		top.push_back(Vector3(cos(a) * top_r, top_y, sin(a) * top_r))
	faces.push_back(bottom)
	faces.push_back(top)
	for i in 6:
		var j := (i + 1) % 6
		faces.push_back(quad(bottom[i], bottom[j], top[j], top[i]))
	var column := solid(faces)

	var c := Vector3(0, height * 0.98, 0)
	var r := radius * 0.5
	var crown := [Vector3(r, 0, 0), Vector3(0, 0, r), Vector3(-r, 0, 0), Vector3(0, 0, -r)]
	var up := c + Vector3(0, r * 1.5, 0)
	var down := c - Vector3(0, r * 1.1, 0)
	var crystal := []
	for i in 4:
		crystal.push_back(tri(up, c + crown[i], c + crown[(i + 1) % 4]))
		crystal.push_back(tri(down, c + crown[(i + 1) % 4], c + crown[i]))
	var st := SurfaceTool.new()
	st.append_from(column, 0, Transform3D.IDENTITY)
	st.append_from(solid(crystal), 0, Transform3D.IDENTITY)
	return st.commit()


## The gate, in metres: an eight-sided ring standing up, facing +z.
static func gate(outer: float, inner: float, depth: float) -> ArrayMesh:
	var faces := []
	for i in 8:
		var a0 := TAU * (i - 0.5) / 8.0
		var a1 := TAU * (i + 0.5) / 8.0
		var o0 := Vector3(cos(a0) * outer, sin(a0) * outer, 0)
		var o1 := Vector3(cos(a1) * outer, sin(a1) * outer, 0)
		var i0 := Vector3(cos(a0) * inner, sin(a0) * inner, 0)
		var i1 := Vector3(cos(a1) * inner, sin(a1) * inner, 0)
		var dz := Vector3(0, 0, depth * 0.5)
		faces.append_array([
			quad(o0 + dz, o1 + dz, i1 + dz, i0 + dz), quad(o0 - dz, i0 - dz, i1 - dz, o1 - dz),
			quad(o0 - dz, o1 - dz, o1 + dz, o0 + dz), quad(i0 - dz, i0 + dz, i1 + dz, i1 - dz),
			quad(o0 - dz, o0 + dz, i0 + dz, i0 - dz), quad(o1 - dz, i1 - dz, i1 + dz, o1 + dz)])
	return _merge_runs(faces, 6)


## A box from corner to corner, in metres.
static func box(lo: Vector3, hi: Vector3) -> Array:
	var outline := PackedVector2Array([Vector2(lo.x, lo.z), Vector2(hi.x, lo.z), Vector2(hi.x, hi.z), Vector2(lo.x, hi.z)])
	return prism_faces(outline, lo.y, hi.y)


## Faces in runs of `run`, each run its own solid, joined into one mesh.
static func _merge_runs(faces: Array, run: int) -> ArrayMesh:
	var st := SurfaceTool.new()
	var i := 0
	while i < faces.size():
		st.append_from(solid(faces.slice(i, i + run)), 0, Transform3D.IDENTITY)
		i += run
	return st.commit()


## A bullet: a long thin octahedron from x -0.5 to 0.5, scaled by the world.
static func bullet() -> ArrayMesh:
	var head := Vector3(0.5, 0, 0)
	var tail := Vector3(-0.5, 0, 0)
	var ring := [Vector3(0.15, 0.5, 0), Vector3(0.15, 0, 0.5), Vector3(0.15, -0.5, 0), Vector3(0.15, 0, -0.5)]
	var faces := []
	for i in 4:
		faces.push_back(tri(head, ring[i], ring[(i + 1) % 4]))
		faces.push_back(tri(tail, ring[(i + 1) % 4], ring[i]))
	return solid(faces)


## A unit annulus for ring.gdshader: UV.x 0 inside, 1 outside.
static func ring(segments := 72) -> ArrayMesh:
	var verts := PackedVector3Array()
	var uv := PackedVector2Array()
	for i in segments:
		var a0 := TAU * i / segments
		var a1 := TAU * (i + 1) / segments
		var d0 := Vector3(cos(a0), 0, sin(a0))
		var d1 := Vector3(cos(a1), 0, sin(a1))
		for p in [[d0, 0.0], [d1, 1.0], [d1, 0.0], [d0, 0.0], [d0, 1.0], [d1, 1.0]]:
			verts.push_back(p[0])
			uv.push_back(Vector2(p[1], 0.0))
	return _plain(verts, uv)


## A quad for spark.gdshader: x 0 (head) to 1 (tail), y -0.5 to 0.5.
static func streak() -> ArrayMesh:
	var verts := PackedVector3Array([Vector3(0, -0.5, 0), Vector3(1, -0.5, 0), Vector3(1, 0.5, 0),
		Vector3(0, -0.5, 0), Vector3(1, 0.5, 0), Vector3(0, 0.5, 0)])
	var uv := PackedVector2Array([Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 0), Vector2(1, 1), Vector2(0, 1)])
	return _plain(verts, uv)


## The field's frame: four low walls round it, in metres.
static func frame(width: float, depth: float, thick: float, height: float) -> ArrayMesh:
	var hw := width * 0.5
	var hd := depth * 0.5
	var faces := []
	var walls := [
		[Vector2(-hw - thick, -hd - thick), Vector2(hw + thick, -hd)],
		[Vector2(-hw - thick, hd), Vector2(hw + thick, hd + thick)],
		[Vector2(-hw - thick, -hd), Vector2(-hw, hd)],
		[Vector2(hw, -hd), Vector2(hw + thick, hd)]]
	for w in walls:
		var a: Vector2 = w[0]
		var b: Vector2 = w[1]
		var outline := PackedVector2Array([a, Vector2(b.x, a.y), b, Vector2(a.x, b.y)])
		faces.append_array(prism_faces(outline, -0.05, height))
	return _merge(faces)


static func _tetra(r: float) -> Array:
	var top := Vector3(0, r, 0)
	var base := []
	for i in 3:
		var a := TAU * i / 3.0
		base.push_back(Vector3(cos(a) * r * 0.94, -r * 0.33, sin(a) * r * 0.94))
	return [tri(top, base[0], base[1]), tri(top, base[1], base[2]), tri(top, base[2], base[0]), tri(base[0], base[2], base[1])]


## Separate solids in one mesh: each prism is turned out from its own centre.
static func _merge(faces: Array) -> ArrayMesh:
	var parts := []
	# Prisms arrive as runs of (2 + sides) faces; build each alone, then join.
	var i := 0
	while i < faces.size():
		var sides: int = faces[i].size()
		var part := faces.slice(i, i + 2 + sides)
		parts.push_back(solid(part))
		i += 2 + sides
	var st := SurfaceTool.new()
	for p in parts:
		st.append_from(p, 0, Transform3D.IDENTITY)
	return st.commit()


static func _plain(verts: PackedVector3Array, uv: PackedVector2Array) -> ArrayMesh:
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_TEX_UV] = uv
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh
