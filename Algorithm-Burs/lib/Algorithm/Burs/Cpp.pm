package Algorithm::Burs::Cpp;

use strict;
use warnings;

use Algorithm::Burs;
use File::Basename qw();

my $TEMPLATE_H = <<'EOT';
#ifndef _ALGORITHM_BURS_CPP_$NAME_UC
#define _ALGORITHM_BURS_CPP_$NAME_UC

#include <vector>

$INTERFACE_HEADER

// TODO namespace

class $NAME {
public:
$RESULT_TYPE
protected:
    struct State {
        int label, arity;
        std::vector<State *> args;
        $NODE_TYPE node;
        Result result;
    };

public:
    enum Functor {
$FUNCTORS
    };

    $NAME();
    ~$NAME();

    Result run($NODE_TYPE node);

protected:
    void reduce(State *state);

private:
    State *label($NODE_TYPE node);
    void reduce(State *state, int tag);
    virtual void reduce_action(State *state, int rule_id) = 0;

    std::vector<State *> labeled_states;
};

#endif // _ALGORITHM_BURS_CPP_$NAME_UC
EOT

my $TEMPLATE_CPP = <<'EOT';
#include "$HEADER_NAME"

#include <tr1/unordered_map>
#include <cstdlib>

#define COUNT(A) (sizeof(A) / sizeof((A)[0]))

$IMPLEMENTATION_HEADER

namespace std { namespace tr1 {
    template<>
    struct hash<$NAME::Functor> {
        std::size_t operator()($NAME::Functor key) const {
            return hasher(key);
        }
        hash<int> hasher;
    };
}}

namespace {
    typedef int Label;
    typedef int RepState;

    struct Transition {
        std::vector<RepState> arg_labels;
        Label label;

        Transition(RepState *states, int count, Label _label) :
            arg_labels(states, states + count), label(_label)
        { }
    };

    struct Rule {
        std::vector<int> args;

        Rule(int *_args, int count) :
            args(_args, _args + count)
        { }
    };

    std::tr1::unordered_map<Label, RepState>
    _make_map(int *data, int size) {
        std::tr1::unordered_map<Label, RepState> res;

        for (int i = 0; i < size; i += 2)
            res[data[i]] = data[i + 1];

        return res;
    }

    typedef std::vector<Transition> TransitionTable;
    typedef std::tr1::unordered_map<Label, RepState> StateMap;
    typedef std::tr1::unordered_map<int, std::vector<int> > RuleMap;
    typedef std::vector<StateMap> OpMap;
    typedef $NAME::Functor Functor;

    std::tr1::unordered_map<Functor, int> leaves_map;
    std::tr1::unordered_map<Functor, TransitionTable> transition_tables;
    std::tr1::unordered_map<Functor, OpMap> op_maps;
    std::tr1::unordered_map<int, Rule> rules;
    std::vector<RuleMap> states;
$INIT_DATA
    struct Init$NAME {
        Init$NAME() {
            for (int i = 0; i < COUNT(_leaves_map); i += 2)
                leaves_map[Functor(_leaves_map[i])] = _leaves_map[i + 1];

$INIT_CODE
        }
    } init_$NAME_LC;
}

$NAME::$NAME() {
}

$NAME::~$NAME() {
    for (unsigned i = 0; i < labeled_states.size(); ++i)
        delete labeled_states[i];
    labeled_states.clear();
}

$NAME::Result
$NAME::run($NODE_TYPE node)
{
    State *state = label(node);

    reduce(state, $ROOT_LABEL);

    Result result = state->result;

    for (unsigned i = 0; i < labeled_states.size(); ++i)
        delete labeled_states[i];
    labeled_states.clear();

    return result;
}

$NAME::State *
$NAME::label($NODE_TYPE node)
{
    int arity, extra_arity;
    Functor functor_id;
    State *state = new State();

    FUNCTOR_$NAME_UC(node, (int *)&functor_id, &arity, &extra_arity);
    state->node = node;
    state->arity = arity;

    labeled_states.push_back(state);

    if (arity) {
        const TransitionTable &transitions = transition_tables[functor_id];
        const OpMap &op_map = op_maps[functor_id];
        std::vector<int> arg_labels;

        arg_labels.reserve(arity);
        for (int i = 0; i < arity; ++i) {
            State *arg_state = label(ARG_$NAME_UC(node, i));
            const StateMap &state_map = op_map[i];
            StateMap::const_iterator rep_state = state_map.find(arg_state->label);
            if (rep_state == state_map.end())
                abort();

            state->args.push_back(arg_state);
            arg_labels.push_back(rep_state->second);
        }

        // TODO use a lookup table
        state->label = -1;
        for (int i = 0; i < transitions.size(); ++i) {
            const Transition &transition = transitions[i];

            if (transition.arg_labels == arg_labels) {
                state->label = transition.label;
                break;
            }
        }

        if (state->label == -1)
            std::abort();

        for (int i = arity; i < extra_arity; ++i) {
            State *arg_state = label(ARG_$NAME_UC(node, i));

            state->args.push_back(arg_state);
        }

        return state;
    } else {
        state->label = leaves_map[functor_id];

        for (int i = arity; i < extra_arity; ++i) {
            State *arg_state = label(ARG_$NAME_UC(node, i));

            state->args.push_back(arg_state);
        }

        return state;
    }
}

void
$NAME::reduce(State *state)
{
    reduce(state, $DEFAULT_LABEL);
}

void
$NAME::reduce(State *state, int tag)
{
    const RuleMap &rule_map = states[state->label];
    RuleMap::const_iterator rule_it = rule_map.find(tag);
    if (rule_it == rule_map.end())
        abort();
    const std::vector<int> &rule_ids = rule_it->second;
    int rule_id = rule_ids.front();
    std::tr1::unordered_map<int, Rule>::iterator r = rules.find(rule_id);
    if (r == rules.end())
        abort();
    const Rule &rule = r->second;

    for (int i = 0; i < rule.args.size(); ++i) {
        State *arg_state = state->args[i];

        reduce(arg_state, rule.args.at(i));
    }

    for (int i = 1; i < rule_ids.size(); ++i)
        reduce_action(state, rule_ids.at(i));
}

EOT

my $TEMPLATE_RULES = <<'EOT';
$RULES_HEADER

void
$RULES_NAME::reduce_action(State *state, int rule_id)
{
    switch (rule_id) {
$RULES
    }
}

EOT

sub new {
    my ($class) = @_;
    my $self = bless {
        burs                    => Algorithm::Burs->new,
        rule_map                => {},
        fields                  => [],
        interface_headers       => [],
        implementation_headers  => [],
        node_type               => undef,
        root_label              => undef,
        default_label           => undef,
    }, $class;

    return $self;
}

sub add_interface_header {
    my ($self, $header) = @_;

    push @{$self->{interface_headers}}, $header;
}

sub add_implementation_header {
    my ($self, $header) = @_;

    push @{$self->{implementation_headers}}, $header;
}

sub add_rules_header {
    my ($self, $header) = @_;

    push @{$self->{rules_headers}}, $header;
}

sub set_node_type {
    my ($self, $type) = @_;

    $self->{node_type} = $type;
}

sub add_result_field {
    my ($self, $type, $name) = @_;

    push @{$self->{fields}}, [$type, $name];
}

sub set_result_type {
    my ($self, $type) = @_;

    $self->{result_type} = $type;
}

sub add_rule {
    my ($self, $weight, $code, $label, $rule) = @_;
    my $rule_id = $self->{burs}->add_rule($weight, $label, $rule);

    push @{$self->{rule_map}{$code}}, $rule_id if $code;
}

sub set_root_label {
    my ($self, $label) = @_;

    $self->{root_label} = $self->{burs}->add_tag($label);
}

sub set_default_label {
    my ($self, $label) = @_;

    # TODO make the default label per-rule
    # TODO check there is a chain rule root -> default
    $self->{default_label} = $self->{burs}->add_tag($label);
}

sub _sortedmap {
    my ($hash) = @_;

    return map { $_ => $hash->{$_} } sort keys %$hash;
}

sub generate {
    my ($self, $class, $rules_class, $header_file, $source_file, $rules_file) = @_;
    # leaves_map, op_map, states, transitions
    my $tables = $self->{burs}->generate_tables;
    my $functors = $self->{burs}->functors;
    my ($rule_code, $init_data, $init_code) = ('', '', '');
    my %has_code;

    for my $code (sort keys %{$self->{rule_map}}) {
        my $cases = join "\n",
                    map  sprintf("case %d:", $_),
                         @{$self->{rule_map}{$code}};
        $has_code{$_} = 1 for @{$self->{rule_map}{$code}};
        $rule_code .= sprintf <<'EOT', $cases, $code;
%s {
    %s
    break;
}
EOT
    }

    $init_data .= sprintf "int _leaves_map[] = {%s};\n",
                          join(", ", _sortedmap($tables->{leaves_map}));

    my $transitions = $tables->{transitions};
    for my $functor_id (sort keys %$transitions) {
        my $i = 0;
        for my $entry (@{$transitions->{$functor_id}}) {
            my $data = sprintf "_states_%d_%d", $functor_id, $i;
            $init_data .= sprintf "RepState %s[] = {%s};\n", $data, join(", ", @{$entry}[0..$#$entry-1]);
            $init_code .= sprintf "transition_tables[Functor(%d)].push_back(Transition(%s, COUNT(%s), %d));\n", $functor_id, $data, $data, $entry->[-1];
            ++$i;
        }
    }

    my $op_map = $tables->{op_map};
    for my $functor_id (sort keys %$op_map) {
        my $i = 0;
        for my $entry (@{$op_map->{$functor_id}}) {
            my $data = sprintf "_op_map_%d_%d", $functor_id, $i;
            $init_data .= sprintf "int %s[] = {%s};\n", $data, join(", ", _sortedmap($entry));
            $init_code .= sprintf "op_maps[Functor(%d)].push_back(_make_map(%s, COUNT(%s)));\n", $functor_id, $data, $data;
            ++$i;
        }
    }

    my $states = $tables->{states};
    my $i = 0;
    for my $map (@$states) {
        $init_code .= "states.push_back(RuleMap());\n";
        $init_code .= "{\n";
        $init_code .= "    RuleMap &state = states[states.size() - 1];\n";
        for my $state (sort keys %$map) {
            my $data = sprintf "_state_map_%d_%d", $i, $state;
            $init_data .= sprintf "int %s[] = {%s};\n", $data, join(", ", $map->{$state}->[0], grep $has_code{$_}, @{$map->{$state}});
            $init_code .= sprintf "    state[%d].assign(%s, %s + COUNT(%s));\n", $state, $data, $data, $data;
        }
        $init_code .= "}\n";
        ++$i;
    }

    my $rules = $self->{burs}->rules;
    for my $rule_id (0..$#$rules) {
        my $rule = $rules->[$rule_id];
        if ($rule->{type} eq 'functor' && $rule->{args}) {
            my $data = sprintf "_rule_args_%d", $rule_id;
            $init_data .= sprintf "int %s[] = {%s};\n", $data, join(", ", @{$rule->{args}});
            $init_code .= sprintf "rules.insert(std::make_pair(%d, Rule(%s, COUNT(%s))));\n", $rule_id, $data, $data;
        } else {
            $init_code .= sprintf "rules.insert(std::make_pair(%d, Rule(NULL, 0)));\n", $rule_id;
        }
    }

    my $result_type;
    if ($self->{result_type}) {
        $result_type = sprintf <<'EOT', $self->{result_type};
    typedef %s Result;
EOT
    } else {
        my $fields = _indent(8, join ";\n", map "$_->[0] $_->[1]", @{$self->{fields}});
        $result_type = sprintf <<'EOT', $fields,
    union Result {
%s;
    };
EOT
    }

    my %vars = (
        NAME_UC     => uc $class,
        NAME_LC     => lc $class,
        NAME        => $class,
        RULES_NAME  => $rules_class,
        FUNCTORS    => _indent(8, join ",\n", map "$_ = $functors->{$_}", sort keys %$functors),
        INTERFACE_HEADER =>
            (join "\n", @{$self->{interface_headers}}),
        IMPLEMENTATION_HEADER =>
            (join "\n", @{$self->{implementation_headers}}),
        RULES_HEADER =>
            (join "\n", @{$self->{rules_headers}}),
        HEADER_NAME => File::Basename::basename($header_file),
        NODE_TYPE   => $self->{node_type},
        RESULT_TYPE => $result_type,
        INIT_DATA   => _indent(4, $init_data),
        INIT_CODE   => _indent(12, $init_code),
        RULES       => _indent(4, $rule_code),
        ROOT_LABEL  => $self->{root_label},
        DEFAULT_LABEL => $self->{default_label},
    );

    # print Data::Dumper::Dumper($tables, $rules);

    return {
        matcher_header => _replace_vars($TEMPLATE_H, \%vars),
        matcher_source => _replace_vars($TEMPLATE_CPP, \%vars),
        emitter_source => _replace_vars($TEMPLATE_RULES, \%vars),
    };
}

sub _replace_vars {
    my ($text, $vars) = @_;

    return $text =~ s{\$(\w+)}{
        exists $vars->{$1} or die "Undefined variable '$1'";
        $vars->{$1}
    }erg;
}

sub _indent {
    my ($level, $text) = @_;
    my $ind = ' ' x $level;

    return $text =~ s{^}{$ind}rmg;
}

1;
