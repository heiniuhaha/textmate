#include "types.h"
#include "parse.h"
#include "scope.h"
#include <oak/oak.h>

struct dummy_t
{
	dummy_t (char const* f) : f(f) { fprintf(stderr, "%s\n", f); }
	~dummy_t ()                    { fprintf(stderr, "~%s\n", f); }
	char const* f;
};

// #define ENTER dummy_t _dummy(__FUNCTION__)
#define ENTER

namespace scope
{
	namespace types
	{
		bool prefix_match (std::vector<scope::types::atom_t> const& lhs, std::vector<scope::types::atom_t> const& rhs)
		{
			ENTER;
			if(lhs.size() > rhs.size())
				return false;

			for(size_t i = 0; i < lhs.size(); ++i)
			{
				assert(i < lhs.size()); assert(i < rhs.size());
				if(lhs[i] != rhs[i] && lhs[i] != atom_any)
					return false;
			}

			return true;
		}

		bool path_t::does_match (path_t const& lhs, path_t const& path, double* rank) const
		{
			ENTER;
			//printf("scope selector:%s\n", this->to_s().c_str());
			size_t i = path.scopes.size(); // “source.ruby string.quoted.double constant.character”
			const size_t size_i = i;
			size_t j = scopes.size();      // “string > constant $”
			const size_t size_j = j;
			
			bool anchor_to_bol = this->anchor_to_bol;
			bool anchor_to_eol = this->anchor_to_eol;
			//printf("scope selector: anchor_to_bol:%s anchor_to_eol:%s\n", anchor_to_bol?"yes":"no", anchor_to_eol?"yes":"no");
			
			bool check_next = false;
			size_t reset_i, reset_j;
			double reset_score = 0;
			double score = 0;
			double power = 0;
			while(j <= i && j)
			{
				assert(i); assert(j);
				assert(i-1 < path.scopes.size());
				assert(j-1 < scopes.size());

				bool anchor_to_previous = scopes[j-1].anchor_to_previous;
				//printf("scope selector:%s anchor_to_previous:%s check_next:%s\n", types::to_s(scopes[j-1]).c_str(), anchor_to_previous?"yes":"no", check_next?"yes":"no");
				
				if((anchor_to_previous || (anchor_to_bol && j == 1)) && !check_next)
				{
					reset_score = score;
					reset_i = i;
					reset_j = j;
				}

				power += path.scopes[i-1].atoms.size();
				if(prefix_match(scopes[j-1].atoms, path.scopes[i-1].atoms))
				{
					for(size_t k = 0; k < scopes[j-1].atoms.size(); ++k)
						score += 1 / pow(2, power - k);
					--j;
					check_next = anchor_to_previous;
				}
				else if(check_next)
				{
					i = reset_i;
					j = reset_j;
					score = reset_score;
					check_next = false;
				}
				--i;
				// if the outer loop has run once but the inner one has not, it is not an anchor to eol
				if(anchor_to_eol)
				{
					//printf("anchor_to_eol: i:%zd size_i:%zd j:%zd size_j:%zd\n",i,size_i,j,size_j);
					if(i != size_i && j == size_j)
						break;
					else
						anchor_to_eol = false;
				}
				
				
				if(anchor_to_bol && j == 0 && i != 0) {
					i = reset_i - 1;
					j = reset_j;
					score = reset_score;
					check_next = false;
				}
				
			}
			if(j == 0 && rank)
				*rank = score;
			return j == 0;
		}

		bool composite_t::does_match (path_t const& lhs, path_t const& rhs, double* rank) const
		{
			ENTER;
			bool res = false;
			if(rank)
			{
				double r, sum = 0;
				iterate(expr, expressions)
				{
					expression_t::op_t op = expr->op;

					bool local = expr->selector->does_match(lhs, rhs, &r);
					if(local)
						sum = std::max(r, sum);

					if(expr->negate)
						local = !local;

					switch(op)
					{
						case expression_t::op_none:  res = local;         break;
						case expression_t::op_or:    res = res || local;  break;
						case expression_t::op_and:   res = res && local;  break;
						case expression_t::op_minus: res = res && !local; break;
					}
				}

				if(res)
					*rank = sum;

				return res;
			}

			iterate(expr, expressions)
			{
				expression_t::op_t op = expr->op;
				if(res && op == expression_t::op_or) // skip ORs when we already have a true value
					continue;
				else if(!res && op == expression_t::op_and) // skip ANDs when we have a false value
					continue;
				else if(!res && op == expression_t::op_minus) // skip intersection when we have a false value
					continue;

				bool local = expr->selector->does_match(lhs, rhs, rank);
				if(expr->negate)
					local = !local;

				switch(op)
				{
					case expression_t::op_none:  res = local;         break;
					case expression_t::op_or:    res = res || local;  break;
					case expression_t::op_and:   res = res && local;  break;
					case expression_t::op_minus: res = res && !local; break;
				}
			}
			return res;
		}

		bool selector_t::does_match (path_t const& lhs, path_t const& rhs, double* rank) const
		{
			ENTER;
			if(rank)
			{
				bool res = false;
				double r, sum = 0;
				iterate(composite, composites)
				{
					if(composite->does_match(lhs, rhs, &r))
					{
						sum = std::max(r, sum);
						res = true;
					}
				}
				if(res)
					*rank = sum;
				return res;
			}

			iterate(composite, composites)
			{
				if(composite->does_match(lhs, rhs, rank))
					return true;
			}
			return false;
		}

		bool group_t::does_match (path_t const& lhs, path_t const& rhs, double* rank) const
		{
			ENTER;
			return selector.does_match(lhs, rhs, rank);
		}

		bool filter_t::does_match (path_t const& lhs, path_t const& rhs, double* rank) const
		{
			ENTER;
			if(filter == both && rank)
			{
				double r1, r2;
				if(selector->does_match(lhs, lhs, &r1) && selector->does_match(rhs, rhs, &r2))
				{
					*rank = std::max(r1, r2);
					return true;
				}
				return false;
			}

			switch(filter)
			{
				case left:  return selector->does_match(lhs, lhs, rank);
				case right: return selector->does_match(rhs, rhs, rank);
				case both:  return selector->does_match(lhs, lhs, rank) && selector->does_match(rhs, rhs, rank);
			}
			return false;
		}

	} /* types */
	
} /* scope */ 
