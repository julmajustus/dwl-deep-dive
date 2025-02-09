/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   btrtile.c                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jmakkone <jmakkone@student.hive.fi>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2024/12/15 00:26:07 by jmakkone          #+#    #+#             */
/*   Updated: 2025/02/08 04:52:00 by jmakkone         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

typedef enum {
	COORD_X,
	COORD_Y
} CoordType;

typedef struct LayoutNode {
	unsigned int is_client_node;
	unsigned int split_vertically;
	float split_ratio;
	struct LayoutNode *left;
	struct LayoutNode *right;
	struct LayoutNode *split_node;
	Client *client;
} LayoutNode;

struct TreeLayout {
	LayoutNode *root[TAGCOUNT + 1];
	struct wl_list tiled_clients[TAGCOUNT + 1];
};

static void add_client_to_tiled_list(Client *c, struct wl_list *tiled_clients);
static void apply_layout(Monitor *m, LayoutNode *node,
						struct wlr_box area, unsigned int is_root);
static void btrtile(Monitor *m);
static LayoutNode *create_client_node(Client *c);
static LayoutNode *create_split_node(unsigned int split_vertically,
									LayoutNode *left, LayoutNode *right);
static void destroy_node_tree(LayoutNode *node);
static void destroy_tree_layout(Monitor *m);
static LayoutNode *find_client_node(LayoutNode *node, Client *c);
static LayoutNode *find_split_node(LayoutNode *root, LayoutNode *child);
static LayoutNode *find_suitable_split_node(LayoutNode *client_node,
											unsigned int need_vertical);
static LayoutNode *find_closest_client_node(LayoutNode *split_node,
											enum Direction dir, int current_x,
											int current_y, LayoutNode **closest,
											int *closest_dist);
static unsigned int get_client_center(LayoutNode *node, CoordType type);
static unsigned int get_current_tag(Monitor *m);
static void init_tree_layout(Monitor *m);
static void insert_client(Monitor *m, Client *focused, Client *new_client,
						LayoutNode **root, struct wl_list *tiled_clients);
static unsigned int is_client_tiled(Client *c, struct wl_list *tiled_clients);
static LayoutNode *remove_client_node(LayoutNode *node, Client *c);
static void remove_client(Monitor *m, Client *c,
						LayoutNode **root, struct wl_list *tiled_clients);
static void setratio_h(const Arg *arg);
static void setratio_v(const Arg *arg);
static void swapclients(const Arg *arg);
static Client *xytoclient(double x, double y, uint32_t tag);

static int resizing_from_mouse = 0;
static double resize_last_update_x, resize_last_update_y;
static uint32_t last_resize_time = 0;

void
add_client_to_tiled_list(Client *c, struct wl_list *tiled_clients)
{
	if (!is_client_tiled(c, tiled_clients))
		wl_list_insert(tiled_clients, &c->link_tiled);
}

void
apply_layout(Monitor *m, LayoutNode *node,
			struct wlr_box area, unsigned int is_root)
{
	float ratio;
	int mid;
	unsigned int e = m->gaps;
	struct wlr_box left_area, right_area;

	if (!node)
		return;

	if (is_root && e) {
		area.x += gappx;
		area.y += gappx;
		area.width -= 2 * gappx;
		area.height -= 2 * gappx;
	}

	if (node->is_client_node) {
		resize(node->client, area, 0);
		node->client->old_geom = area;
		return;
	}

	ratio = node->split_ratio;
	if (ratio == 0.0f)
		ratio = 0.5f;
	if (ratio < 0.05f)
		ratio = 0.05f;
	if (ratio > 0.95f)
		ratio = 0.95f;

	if (node->split_vertically) {
		mid = (int)(area.width * ratio);
		left_area = (struct wlr_box){ area.x, area.y, mid, area.height};
		right_area = (struct wlr_box){ area.x + mid, area.y,
			area.width - mid, area.height};

		if (e) {
			left_area.width -= gappx / 2;
			right_area.x += gappx / 2;
			right_area.width -= gappx / 2;
		}
	} else {
		mid = (int)(area.height * ratio);
		left_area = (struct wlr_box){ area.x, area.y, area.width, mid };
		right_area = (struct wlr_box){ area.x, area.y + mid,
			area.width, area.height - mid };

		if (e) {
			left_area.height -= gappx / 2;
			right_area.y += gappx / 2;
			right_area.height -= gappx / 2;
		}
	}

	apply_layout(m, node->left, left_area, 0);
	apply_layout(m, node->right, right_area, 0);
}

void btrtile(Monitor *m)
{
	uint32_t active_tags = m->tagset[m->seltags];
	unsigned int n = 0, found = 0, curtag;
	Client *c, *cc, *tmp, *cur, *focused_client = NULL;
	LayoutNode **root_ptr;
	struct wl_list current_clients, *tiled_clients;
	struct wlr_box full_area = m->w;

	/* We skip handling clients in btrtile if multiple tags are selected */
	if (!m->tree_layout || (active_tags && (active_tags & (active_tags - 1))))
		return;
	curtag = get_current_tag(m);
	root_ptr = &m->tree_layout->root[curtag];
	tiled_clients = &m->tree_layout->tiled_clients[curtag];
	wl_list_for_each_reverse(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
			n++;
	}

	/* If no visible clients, clear the node tree and tiled_clients list. */
	if (n == 0) {
		destroy_node_tree(*root_ptr);
		*root_ptr = NULL;
		wl_list_init(tiled_clients);
		return;
	}

	focused_client = xytoclient(cursor->x, cursor->y, curtag);
	/* If no focused client found, pick the first visible */
	if (!focused_client) {
		wl_list_for_each_reverse(c, &clients, link) {
			if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen) {
				focused_client = c;
				break;
			}
		}
	}

	/* Build a temporary list of currently visible tiled clients.
	 * If new clients are found add them to tree.*/
	wl_list_init(&current_clients);
	wl_list_for_each_reverse(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen) {
			found = 0;
			/* If client has multiple tags set, we might create infinite
			 * point to self loops by adding the same client to multiple
			 * tiled_clients lists, so hacky way to prevent that is to revert
			 * clients active tags to current active tag if multiple
			 * tags are selected. */
			if (c->tags != 1u << curtag)
				c->tags = 1u << curtag;
			wl_list_for_each(cc, tiled_clients, link_tiled) {
				if (cc == c) {
					found = 1;
					break;
				}
			}
			if (!found) {
				insert_client(m, focused_client, c, root_ptr, tiled_clients);
				/* Recursion failsafe to handle inserted clients individually
				 * if a batch of new clients is added. */
				apply_layout(m, *root_ptr, full_area, 1);
				btrtile(m);
				return;
			}
			wl_list_insert(&current_clients, &c->link_temp);
		}
	}

	/* Compare the current list of clients to the previous one and remove
	 * clients that no longer exist on the current tag.
	 * This handles cases where user moves clients to other tags.
	 * When client is closed or killed we manage the list and tree clean up in
	 * destroynotify. */
	wl_list_for_each_safe(cc, tmp, tiled_clients, link_tiled) {
		found = 0;
		wl_list_for_each(cur, &current_clients, link_temp) {
			if (cur == cc) {
				found = 1;
				break;
			}
		}
		if (!found)
			remove_client(m, cc, root_ptr, tiled_clients);
	}

	/* Rebuild the updated client list */
	wl_list_init(tiled_clients);
	wl_list_for_each(cur, &current_clients, link_temp) {
		wl_list_insert(tiled_clients, &cur->link_tiled);
	}

	/* Tile visible clients. */
	apply_layout(m, *root_ptr, full_area, 1);
}

LayoutNode *
create_client_node(Client *c)
{
	LayoutNode *node = calloc(1, sizeof(LayoutNode));

	if (!node)
		return NULL;
	node->is_client_node = 1;
	node->split_ratio = 0.5f;
	node->client = c;
	return node;
}

LayoutNode *
create_split_node(unsigned int split_vertically,
				LayoutNode *left, LayoutNode *right)
{
	LayoutNode *node = calloc(1, sizeof(LayoutNode));

	if (!node)
		return NULL;
	node->is_client_node = 0;
	node->split_ratio = 0.5f;
	node->split_vertically = split_vertically;
	node->left = left;
	node->right = right;
	if (left)
		left->split_node = node;
	if (right)
		right->split_node = node;
	return node;
}

unsigned int
is_client_tiled(Client *c, struct wl_list *tiled_clients)
{
	Client *cc;
	wl_list_for_each(cc, tiled_clients, link_tiled) {
		if (cc == c)
			return 1;
	}
	return 0;
}

void
destroy_node_tree(LayoutNode *node)
{
	if (!node)
		return;
	if (!node->is_client_node) {
		destroy_node_tree(node->left);
		destroy_node_tree(node->right);
	}
	free(node);
}

void
destroy_tree_layout(Monitor *m)
{
	if (!m)
		return;
	for (int i = 0; i <= TAGCOUNT; i++) {
		destroy_node_tree(m->tree_layout->root[i]);
	}
}

LayoutNode *
find_client_node(LayoutNode *node, Client *c)
{
	LayoutNode *res;

	if (!node)
		return NULL;
	if (node->is_client_node)
		return (node->client == c) ? node : NULL;
	res = find_client_node(node->left, c);
	return res ? res : find_client_node(node->right, c);
}

LayoutNode *
find_split_node(LayoutNode *root, LayoutNode *child)
{
	LayoutNode *res;

	if (!root || root->is_client_node)
		return NULL;
	if (root->left == child || root->right == child)
		return root;
	res = find_split_node(root->left, child);
	return res ? res : find_split_node(root->right, child);
}

LayoutNode *
find_suitable_split_node(LayoutNode *client_node, unsigned int need_vertical)
{
	LayoutNode *node = client_node;
	unsigned int curtag;

	curtag = get_current_tag(selmon);
	/* If we're starting from a client_node, go up one level first */
	if (node->is_client_node) {
		node = node->split_node ? node->split_node :
			find_split_node(selmon->tree_layout->root[curtag], node);
	}

	/* Climb the tree until we find a node that is not client_node and
	 * match needed orientation. */
	while (node && (node->is_client_node ||
			node->split_vertically != need_vertical)) {
		node = node->split_node ? node->split_node :
			find_split_node(selmon->tree_layout->root[curtag], node);
	}

	return node;
}

LayoutNode *
find_closest_client_node(LayoutNode *split_node, enum Direction dir,
						 int current_x, int current_y, LayoutNode **closest,
						 int *closest_dist)
{
	int client_center_x, client_center_y, dist, is_candidate;
	if (!split_node)
		return NULL;

	if (split_node->is_client_node && split_node->client) {
		client_center_x = get_client_center(split_node, COORD_X);
		client_center_y = get_client_center(split_node, COORD_Y);
		dist = 0;
		is_candidate = 0;

		switch (dir) {
			case DIR_LEFT:
				if (client_center_x < current_x) {
					dist = current_x - client_center_x;
					is_candidate = 1;
				}
				break;
			case DIR_RIGHT:
				if (client_center_x > current_x) {
					dist = client_center_x - current_x;
					is_candidate = 1;
				}
				break;
			case DIR_UP:
				if (client_center_y < current_y) {
					dist = current_y - client_center_y;
					is_candidate = 1;
				}
				break;
			case DIR_DOWN:
				if (client_center_y > current_y) {
					dist = client_center_y - current_y;
					is_candidate = 1;
				}
				break;
			default:
				break;
		}

		if (is_candidate && dist < *closest_dist) {
			*closest_dist = dist;
			*closest = split_node;
		}
	}

	/* Recursively search in left and right split_nodes */
	find_closest_client_node(split_node->left, dir, current_x, current_y,
							closest, closest_dist);
	find_closest_client_node(split_node->right, dir, current_x, current_y,
							closest, closest_dist);

	return *closest;
}

unsigned int
get_client_center(LayoutNode *node, CoordType type)
{
	if (!node || !node->is_client_node || !node->client)
		return 0;

	switch (type) {
		case COORD_X:
			return node->client->old_geom.x + node->client->old_geom.width / 2;
		case COORD_Y:
			return node->client->old_geom.y + node->client->old_geom.height / 2;
		default:
			return 0;
	}
}

unsigned int
get_current_tag(Monitor *m)
{
	uint32_t active;

	if (!m)
		return 0;

	active = m->tagset[m->seltags];
	for (int i = 0; i < TAGCOUNT; i++) {
		if (active & (1u << i))
			return i;
	}
	return 0;
}

void
init_tree_layout(Monitor *m)
{
	if (!m)
		return;
	m->tree_layout = calloc(1, sizeof(TreeLayout));
	for (int i = 0; i <= TAGCOUNT; i++) {
		m->tree_layout->root[i] = NULL;
		wl_list_init(&m->tree_layout->tiled_clients[i]);
	}
}

void
insert_client(Monitor *m, Client *focused, Client *new_client,
			  LayoutNode **root, struct wl_list *tiled_clients)
{
	int mid_x, mid_y;
	LayoutNode *old_root, *client_node, *old_client_node, *new_client_node;
	unsigned int wider;

	/* If there is no root node, inserted client must be the first one.
	 * If there's no focused client we treat the inserted client as a new root node.*/
	if (!*root) {
		*root = create_client_node(new_client);
		add_client_to_tiled_list(new_client, tiled_clients);
		return;
	}
	if (!focused || !(client_node = find_client_node(*root, focused))) {
		old_root = *root;
		*root = create_split_node(1, old_root, create_client_node(new_client));
		add_client_to_tiled_list(new_client, tiled_clients);
		return;
	}
	/* We check the cursor location on splittable area and choose
		 * the new client's position. On horizontal splits left node represent
		 * the upper node and vice versa.*/
	mid_x = focused->old_geom.x + focused->old_geom.width / 2;
	mid_y = focused->old_geom.y + focused->old_geom.height / 2;
	old_client_node = create_client_node(client_node->client);
	new_client_node = create_client_node(new_client);

	wider = focused->old_geom.width >= focused->old_geom.height;
	if (wider) {
		/* Vertical split */
		client_node->split_vertically = 1;
		if (cursor->x <= mid_x) {
			client_node->left = new_client_node;
			client_node->right = old_client_node;
		} else {
			client_node->left = old_client_node;
			client_node->right = new_client_node;
		}
	} else {
		/* Horizontal split */
		client_node->split_vertically = 0;
		if (cursor->y <= mid_y) {
			client_node->left = new_client_node;
			client_node->right = old_client_node;
		} else {
			client_node->left = old_client_node;
			client_node->right = new_client_node;
		}
	}
	/* The old client node becomes the splitnode for the old and new client
		 * nodes.*/
	client_node->is_client_node = 0;
	client_node->client = NULL;
	add_client_to_tiled_list(new_client, tiled_clients);
}

LayoutNode *
remove_client_node(LayoutNode *node, Client *c)
{
	LayoutNode *tmp;
	if (!node)
		return NULL;
	if (node->is_client_node) {
		/* If this client_node is the client we're removing,
		 * return NULL to remove it */
		if (node->client == c) {
			free(node);
			return NULL;
		}
		return node;
	}

	node->left = remove_client_node(node->left, c);
	node->right = remove_client_node(node->right, c);

	/* If one of the client node is NULL after removal and the other is not,
	 * we "lift" the other client node up to replace this split node. */
	if (!node->left && node->right) {
		tmp = node->right;

		/* Save pointer to split node */
		if (tmp)
			tmp->split_node = node->split_node;

		free(node);
		return tmp;
	}

	if (!node->right && node->left) {
		tmp = node->left;

		/* Save pointer to split node */
		if (tmp)
			tmp->split_node = node->split_node;

		free(node);
		return tmp;
	}

	/* If both children exist or both are NULL (empty tree),
	 * return node as is. */
	return node;
}

void
remove_client(Monitor *m, Client *c, LayoutNode **root,
			  struct wl_list *tiled_clients)
{
	Client *cc, *tmp;

	*root = remove_client_node(*root, c);
	wl_list_for_each_safe(cc, tmp, tiled_clients, link_tiled) {
		if (cc == c) {
			wl_list_remove(&cc->link_tiled);
			break;
		}
	}
}

void
setratio_h(const Arg *arg)
{
	Client *sel = focustop(selmon);
	LayoutNode *client_node, *node;
	float new_ratio;

	if (!arg || !sel || !selmon->lt[selmon->sellt]->arrange)
		return;

	client_node = find_client_node(selmon->tree_layout->root[get_current_tag(selmon)], sel);
	if (!client_node)
		return;

	/* Find a suitable vertical node */
	node = find_suitable_split_node(client_node, 1);
	if (!node)
		return;

	new_ratio = arg->f ? (node->split_ratio + arg->f) : 0.5f;
	if (new_ratio < 0.05f)
		new_ratio = 0.05f;
	if (new_ratio > 0.95f)
		new_ratio = 0.95f;

	node->split_ratio = new_ratio;
	/* Skip the arrange if done resizing by mouse,
	 * we call arrange from motionotify */
	if (!resizing_from_mouse) {
		arrange(selmon);
	}
}

void
setratio_v(const Arg *arg)
{
	float new_ratio;
	Client *sel = focustop(selmon);
	LayoutNode *client_node, *node;

	if (!arg || !sel || !selmon->lt[selmon->sellt]->arrange)
		return;

	client_node = find_client_node(selmon->tree_layout->root[get_current_tag(selmon)], sel);
	if (!client_node)
		return;

	/* Find a suitable horizontal node */
	node = find_suitable_split_node(client_node, 0);
	if (!node)
		return;

	new_ratio = arg->f ? (node->split_ratio + arg->f) : 0.5f;
	if (new_ratio < 0.05f) new_ratio = 0.5f;
	if (new_ratio > 0.95f) new_ratio = 0.95f;

	node->split_ratio = new_ratio;
	/* Skip the arrange if done resizing by mouse,
	 * we call arrange from motionotify */
	if (!resizing_from_mouse) {
		arrange(selmon);
	}
}

void
swapclients(const Arg *arg)
{
	Client *tmp, *sel = focustop(selmon);
	enum Direction dir = (enum Direction)arg->i;
	LayoutNode *client_node, *target = NULL, *split_node = NULL;
	unsigned int current_x, current_y, curtag = get_current_tag(selmon);
	int closest_dist = INT_MAX;

	if (!arg || !sel || !selmon->lt[selmon->sellt]->arrange)
		return;

	client_node = find_client_node(selmon->tree_layout->root[curtag], sel);
	if (!client_node)
		return;

	current_x = get_client_center(client_node, COORD_X);
	current_y = get_client_center(client_node, COORD_Y);

	/* For up/down swaps, restrict search within the current horizontal split
	 * node if no suitable horizontal split node is found, default to vertical */
	if (dir == DIR_UP || dir == DIR_DOWN) {
		split_node = find_suitable_split_node(client_node, 0);
		if (!split_node)
			return;
	} else {
		split_node = selmon->tree_layout->root[curtag];
	}

	/* Find the closest client node in the specified direction and swap
	 * the clients */
	find_closest_client_node(split_node, dir, current_x, current_y,
							&target, &closest_dist);

	if (target && target->is_client_node && target->client) {
		tmp = client_node->client;
		client_node->client = target->client;
		target->client = tmp;

		arrange(selmon);
	}
}

Client *
xytoclient(double x, double y, uint32_t tag) {
	Client *c, *closest = NULL;
	double dist, mindist = INT_MAX, dx, dy;

	wl_list_for_each_reverse(c, &selmon->tree_layout->tiled_clients[tag], link_tiled) {
		if (VISIBLEON(c, selmon) && !c->isfloating && !c->isfullscreen &&
			x >= c->geom.x && x <= (c->geom.x + c->geom.width) &&
			y >= c->geom.y && y <= (c->geom.y + c->geom.height)){
			return c;
		}
	}

	/* If no client was found at cursor position fallback to closest. */
	wl_list_for_each_reverse(c, &selmon->tree_layout->tiled_clients[tag], link_tiled) {
		if (VISIBLEON(c, selmon) && !c->isfloating && !c->isfullscreen) {
			dx = 0, dy = 0;

			if (x < c->geom.x)
				dx = c->geom.x - x;
			else if (x > (c->geom.x + c->geom.width))
				dx = x - (c->geom.x + c->geom.width);

			if (y < c->geom.y)
				dy = c->geom.y - y;
			else if (y > (c->geom.y + c->geom.height))
				dy = y - (c->geom.y + c->geom.height);

			dist = sqrt(dx * dx + dy * dy);
			if (dist < mindist) {
				mindist = dist;
				closest = c;
			}
		}
	}
	return closest;
}
