linewidth = 2

no_preemption_dir = "~/playground/results/mkl_dgemm_bolt_patch_1_intvl_0_outer_32_size_4000/latest"

inners = [4, 8, 16, 32]

data_dirs = [
    "~/playground/results/mkl_dgemm_bolt_patch_0_intvl_10000_outer_32_size_4000/latest",
    "~/playground/results/mkl_dgemm_bolt_patch_0_intvl_4000_outer_32_size_4000/latest",
    "~/playground/results/mkl_dgemm_bolt_patch_0_intvl_1000_outer_32_size_4000/latest",
    "~/playground/results/mkl_dgemm_iomp_outer_32_size_4000/latest",
]

opts = [
    {"BOLT (preemptive, intvl=10ms)", "#1f77b4", "solid"  , "square"      },
    {"BOLT (preemptive, intvl=4ms)" , "#d62728", "dot"    , "x-thin"      },
    {"BOLT (preemptive, intvl=1ms)" , "#ff7f0e", "dashdot", "circle"      },
    {"IOMP"                         , "#2ca02c", "dash"   , "diamond-open"},
]

get_statistics_of_files = fn(path, op_fn) ->
    data =
        path
        |> Path.expand()
        |> Path.wildcard()
        |> Enum.map(fn path ->
            File.stream!(path)
            |> Stream.map(&String.split/1)
            |> Stream.map(fn
                [_, "GFLOPS", "=", gflops | _] -> Float.parse(gflops) |> elem(0)
                _                              -> nil
            end)
            |> Stream.filter(&!is_nil(&1))
            |> Enum.to_list()
        end)
        |> List.flatten()
        |> Enum.map(op_fn)
    {Statistics.median(data), Statistics.stdev(data)}
end

{baselines, _} =
    Enum.map(inners, fn inner ->
        get_statistics_of_files.("#{no_preemption_dir}/inner_#{inner}_*.out", &(&1))
    end)
    |> Enum.unzip()

Enum.zip(data_dirs, opts)
|> Enum.map(fn {data_dir, {title, color, dash, symbol}} ->
    {ys, errors} =
        Enum.zip(inners, baselines)
        |> Enum.map(fn {inner, baseline} ->
            get_statistics_of_files.("#{data_dir}/inner_#{inner}_*.out", fn x -> x / baseline end)
        end)
        |> Enum.unzip()
    %{
        x: inners,
        y: ys,
        error_y: %{
            type: "data",
            array: errors,
            visible: true,
        },
        type: "scatter",
        name: title,
        line: %{
            color: color,
            width: linewidth,
            dash: dash,
        },
        marker: %{
            symbol: symbol,
            size:   12,
            line:   %{width: linewidth, color: color},
        },
    }
end)
|> PlotlyEx.plot(%{
    width:  800,
    height: 350,
    xaxis: %{
        type: "log",
        title: %{text: "# of inner threads"},
        showline: true,
        tickvals: inners,
        mirror: "ticks",
    },
    yaxis: %{
        title: %{text: "Relative Performance"},
        showline: true,
        mirror: "ticks",
        range: [0, 1],
    },
    font: %{
        size: 22,
    },
    margin: %{
        l: 70,
        r: 0,
        b: 60,
        t: 10,
        pad: 0,
    },
})
|> PlotlyEx.show()
